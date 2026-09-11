require('dotenv').config();
const express = require('express');
const cors = require('cors');
const helmet = require('helmet');
const compression = require('compression');
const morgan = require('morgan');
const cookieParser = require('cookie-parser');
const path = require('path');
const { db, initializeDatabase, pool } = require('./config/postgres');
const dataSimulator = require('./utils/dataSimulator');

const app = express();
const PORT = process.env.PORT || 5002;

// Initialize PostgreSQL database
const setupDatabase = async () => {
  try {
    await initializeDatabase();
    console.log('✅ Database initialized');

    // Create admin user if not exists
    const bcrypt = require('bcryptjs');
    const adminUser = await db.getUserByUsername('admin');
    if (!adminUser) {
      const hashedPassword = await bcrypt.hash('admin123', 10);
      await db.createUser({
        username: 'admin',
        password: hashedPassword,
        email: 'admin@spine analytics.com',
        role: 'admin'
      });
      console.log('✅ Created default admin user (username: admin, password: admin123)');
    }

    // Load patient data from JSON if exists
    const fs = require('fs');
    const dataPath = path.join(__dirname, '../../data/spine_records.json');
    
    if (fs.existsSync(dataPath)) {
      const rawData = fs.readFileSync(dataPath, 'utf8');
      const patients = JSON.parse(rawData);
      
      for (const patient of patients) {
        try {
          const patientData = {
            patient_id: patient.patient_id,
            age: patient.age,
            sex: patient.sex,
            abnormal_prob: patient.abnormal_prob,
            dl_classification: patient.classification,
            risk_score: patient.risk_score,
            risk_category: patient.risk_category,
            scan_date: patient.date,
            image_path: patient.image_path,
            modality: patient.modality || 'XRAY',
            trabecular_bone_score: patient.trabecular_bone_score,
            cortical_thinning_index: patient.cortical_thinning_index,
            marrow_dark_ratio: patient.marrow_dark_ratio,
            marrow_signal: patient.marrow_signal
          };
          if (patientData.date && !patientData.scan_date) {
            patientData.scan_date = patientData.date;
            delete patientData.date;
          }
          Object.keys(patientData).forEach((key) => patientData[key] == null && delete patientData[key]);
          await db.upsertPatient(patientData);
        } catch (error) {
          console.error(`Error loading patient ${patient.patient_id}:`, error.message);
        }
      }
      
      console.log(`✅ Loaded ${patients.length} patient records from JSON`);
    }

    // Start data simulator if enabled
    if (process.env.ENABLE_SIMULATION === 'true') {
      const intervalMs = parseInt(process.env.SIMULATION_INTERVAL) || 1000;
      setTimeout(() => {
        dataSimulator.start(intervalMs);
      }, 2000);
    }
  } catch (error) {
    console.error('❌ Database setup error:', error);
  }
};

// Initialize database on startup
setupDatabase();

// Security Middleware
app.use(helmet({
    contentSecurityPolicy: {
        directives: {
            defaultSrc: ["'self'"],
            styleSrc: ["'self'", "'unsafe-inline'"],
            scriptSrc: ["'self'"],
            imgSrc: ["'self'", "data:", "https:"],
        },
    },
    hsts: {
        maxAge: 31536000,
        includeSubDomains: true,
        preload: true
    }
}));

// Compression
app.use(compression());

// Logging
if (process.env.NODE_ENV === 'development') {
    app.use(morgan('dev'));
} else {
    app.use(morgan('combined'));
}

// CORS Configuration
app.use(cors({
    origin: process.env.CORS_ORIGIN || 'http://localhost:3000',
    credentials: true,
    optionsSuccessStatus: 200
}));

// Body Parsers
app.use(express.json({ limit: '10mb' }));
app.use(express.urlencoded({ extended: true, limit: '10mb' }));
app.use(cookieParser());

// Rate Limiting - Import from our middleware
const { apiLimiter } = require('./middleware/rateLimiter');
app.use('/api/', apiLimiter);

// Import Routes
const authRoutes = require('./routes/auth');
const patientRoutes = require('./routes/patients');
const analyticsRoutes = require('./routes/analytics');
const vitalsRoutes = require('./routes/vitalsRoutes');
const modelRoutes = require('./routes/models');

// Import Error Handlers
const { notFoundHandler, errorHandler } = require('./middleware/errorHandler');

// Routes
app.use('/api/auth', authRoutes);
app.use('/api/patients', patientRoutes);
app.use('/api/analytics', analyticsRoutes);
app.use('/api/vitals', vitalsRoutes);
app.use('/api/models', modelRoutes);

// Health Check
app.get('/api/health', async (req, res) => {
    try {
        const stats = await db.getPatientStats();
        const simulatorStatus = dataSimulator.getStatus();
        
        res.json({
            status: 'healthy',
            timestamp: new Date().toISOString(),
            uptime: process.uptime(),
            environment: process.env.NODE_ENV || 'development',
            database: {
                connected: true,
                patients: parseInt(stats.total_patients) || 0,
                abnormal_cases: parseInt(stats.abnormal_cases) || 0,
                high_risk: parseInt(stats.high_risk) || 0
            },
            simulator: simulatorStatus
        });
    } catch (error) {
        res.status(500).json({
            status: 'unhealthy',
            error: error.message
        });
    }
});

// 404 Handler
app.use(notFoundHandler);

// Global Error Handler
app.use(errorHandler);

// Start Server
const server = app.listen(PORT, () => {
    console.log('');
    console.log('='.repeat(50));
    console.log('🚀 Spine Analytics Dashboard Server');
    console.log('='.repeat(50));
    console.log(`📡 Server running on port ${PORT}`);
    console.log(`📊 Environment: ${process.env.NODE_ENV || 'development'}`);
    console.log(`🔒 Security features enabled`);
    console.log(`🌐 API available at: http://localhost:${PORT}/api`);
    console.log('');
    console.log('API Endpoints:');
    console.log(`  🔐 Auth:      http://localhost:${PORT}/api/auth`);
    console.log(`  👤 Patients:  http://localhost:${PORT}/api/patients`);
    console.log(`  📈 Analytics: http://localhost:${PORT}/api/analytics`);
    console.log(`  ❤️  Health:    http://localhost:${PORT}/api/health`);
    console.log(`  🧠 Models:    http://localhost:${PORT}/api/models`);
    console.log('='.repeat(50));
    console.log('');
});

// Graceful Shutdown
process.on('SIGTERM', () => {
    console.log('SIGTERM signal received: closing HTTP server');
    server.close(() => {
        console.log('HTTP server closed');
        process.exit(0);
    });
});

process.on('SIGINT', () => {
    console.log('\nSIGINT signal received: closing HTTP server');
    server.close(() => {
        console.log('HTTP server closed');
        process.exit(0);
    });
});

module.exports = app;
