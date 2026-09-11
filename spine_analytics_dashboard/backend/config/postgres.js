const { Pool } = require('pg');
require('dotenv').config();

// PostgreSQL connection pool
const pool = new Pool({
  host: process.env.DB_HOST || 'localhost',
  port: process.env.DB_PORT || 5432,
  user: process.env.DB_USER || 'postgres',
  password: process.env.DB_PASSWORD || 'yourpassword',
  database: process.env.DB_NAME || 'postgres'
});

// Test connection
pool.on('connect', () => {
  console.log('✅ Connected to PostgreSQL database');
});

pool.on('error', (err) => {
  console.error('❌ PostgreSQL connection error:', err);
});

// Initialize database schema
const initializeDatabase = async () => {
  const client = await pool.connect();
  
  try {
    // Create users table
    await client.query(`
      CREATE TABLE IF NOT EXISTS users (
        id SERIAL PRIMARY KEY,
        username VARCHAR(100) UNIQUE NOT NULL,
        password VARCHAR(255) NOT NULL,
        email VARCHAR(255),
        role VARCHAR(50) DEFAULT 'user',
        is_active BOOLEAN DEFAULT true,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
      )
    `);

    // Create patients table with comprehensive spine metrics
    await client.query(`
      CREATE TABLE IF NOT EXISTS patients (
        id SERIAL PRIMARY KEY,
        patient_id VARCHAR(100) UNIQUE NOT NULL,
        age INTEGER,
        sex VARCHAR(10),
        
        -- Deep Learning Results
        normal_prob DECIMAL(5,2),
        abnormal_prob DECIMAL(5,2),
        dl_classification VARCHAR(50),
        
        -- UCI Vertebral Column Analysis
        pelvic_incidence DECIMAL(5,2),
        pelvic_tilt DECIMAL(5,2),
        lumbar_lordosis_angle DECIMAL(5,2),
        sacral_slope DECIMAL(5,2),
        pelvic_radius DECIMAL(5,2),
        grade_of_spondylolisthesis DECIMAL(5,2),
        pathology VARCHAR(100),
        risk_score DECIMAL(5,2),
        risk_category VARCHAR(50),
        
        -- Clinical Scoring
        cobb_angle DECIMAL(5,2),
        scoliosis_severity VARCHAR(50),
        compression_fracture_grade INTEGER,
        bone_density_tscore DECIMAL(4,2),
        bone_density_classification VARCHAR(50),
        fracture_risk_10yr DECIMAL(5,2),
        disc_degeneration_grade INTEGER,
        instability_score INTEGER,
        modality VARCHAR(20) DEFAULT 'XRAY',
        trabecular_bone_score DECIMAL(6,4),
        cortical_thinning_index DECIMAL(6,2),
        marrow_dark_ratio DECIMAL(6,2),
        marrow_signal VARCHAR(80),
        
        -- Metadata
        scan_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        image_path VARCHAR(500),
        processing_time_ms DECIMAL(10,2),
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
      )
    `);

    await client.query(`
      ALTER TABLE patients
        ADD COLUMN IF NOT EXISTS modality VARCHAR(20) DEFAULT 'XRAY',
        ADD COLUMN IF NOT EXISTS trabecular_bone_score DECIMAL(6,4),
        ADD COLUMN IF NOT EXISTS cortical_thinning_index DECIMAL(6,2),
        ADD COLUMN IF NOT EXISTS marrow_dark_ratio DECIMAL(6,2),
        ADD COLUMN IF NOT EXISTS marrow_signal VARCHAR(80)
    `);

    await client.query(`
      CREATE TABLE IF NOT EXISTS ml_model_weights (
        id SERIAL PRIMARY KEY,
        model_name VARCHAR(160) UNIQUE NOT NULL,
        task VARCHAR(100),
        format VARCHAR(40) DEFAULT 'KARNIX_SPINE_WEIGHTS',
        parameter_count INTEGER,
        size_bytes BIGINT NOT NULL,
        sha256 CHAR(64) NOT NULL,
        source_path VARCHAR(500),
        weights BYTEA NOT NULL,
        metadata JSONB DEFAULT '{}'::jsonb,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
      )
    `);

    // Create health vitals table for real-time monitoring
    await client.query(`
      CREATE TABLE IF NOT EXISTS health_vitals (
        id SERIAL PRIMARY KEY,
        patient_id VARCHAR(100) REFERENCES patients(patient_id),
        
        -- Vital Signs
        heart_rate INTEGER,
        spo2 DECIMAL(5,2),
        blood_pressure_systolic INTEGER,
        blood_pressure_diastolic INTEGER,
        temperature DECIMAL(4,2),
        respiratory_rate INTEGER,
        
        -- Activity & Sleep
        steps INTEGER,
        calories_burned INTEGER,
        sleep_hours DECIMAL(4,2),
        sleep_quality VARCHAR(50),
        
        -- Additional Metrics
        stress_level INTEGER,
        activity_level VARCHAR(50),
        
        timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP
      )
    `);

    // Create real-time metrics table for continuous monitoring
    await client.query(`
      CREATE TABLE IF NOT EXISTS realtime_metrics (
        id SERIAL PRIMARY KEY,
        metric_type VARCHAR(100) NOT NULL,
        metric_value DECIMAL(10,2) NOT NULL,
        unit VARCHAR(50),
        patient_id VARCHAR(100),
        timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP
      )
    `);

    // Create indexes for better performance
    await client.query(`
      CREATE INDEX IF NOT EXISTS idx_patients_patient_id ON patients(patient_id);
      CREATE INDEX IF NOT EXISTS idx_patients_scan_date ON patients(scan_date);
      CREATE INDEX IF NOT EXISTS idx_patients_modality ON patients(modality);
      CREATE INDEX IF NOT EXISTS idx_health_vitals_patient_id ON health_vitals(patient_id);
      CREATE INDEX IF NOT EXISTS idx_health_vitals_timestamp ON health_vitals(timestamp);
      CREATE INDEX IF NOT EXISTS idx_realtime_metrics_timestamp ON realtime_metrics(timestamp);
      CREATE INDEX IF NOT EXISTS idx_ml_model_weights_task ON ml_model_weights(task);
    `);

    console.log('✅ Database schema initialized successfully');
  } catch (error) {
    console.error('❌ Error initializing database schema:', error);
    throw error;
  } finally {
    client.release();
  }
};

// Database operations
const db = {
  // Generic query
  query: (text, params) => pool.query(text, params),
  
  // Get client from pool
  getClient: () => pool.connect(),
  
  // User operations
  createUser: async (userData) => {
    const { username, password, email, role } = userData;
    const result = await pool.query(
      'INSERT INTO users (username, password, email, role) VALUES ($1, $2, $3, $4) RETURNING *',
      [username, password, email, role]
    );
    return result.rows[0];
  },
  
  getUserByUsername: async (username) => {
    const result = await pool.query('SELECT * FROM users WHERE username = $1', [username]);
    return result.rows[0];
  },
  
  getAllUsers: async () => {
    const result = await pool.query('SELECT id, username, email, role, is_active, created_at FROM users');
    return result.rows;
  },
  
  // Patient operations
  createPatient: async (patientData) => {
    const fields = Object.keys(patientData);
    const values = Object.values(patientData);
    const placeholders = values.map((_, i) => `$${i + 1}`).join(', ');
    
    const query = `
      INSERT INTO patients (${fields.join(', ')})
      VALUES (${placeholders})
      RETURNING *
    `;
    
    const result = await pool.query(query, values);
    return result.rows[0];
  },

  upsertPatient: async (patientData) => {
    const fields = Object.keys(patientData);
    const values = Object.values(patientData);
    const placeholders = values.map((_, i) => `$${i + 1}`).join(', ');
    const updates = fields
      .filter(field => field !== 'patient_id')
      .map(field => `${field} = EXCLUDED.${field}`)
      .join(', ');
    const query = `
      INSERT INTO patients (${fields.join(', ')})
      VALUES (${placeholders})
      ON CONFLICT (patient_id) DO UPDATE SET
        ${updates},
        created_at = patients.created_at
      RETURNING *
    `;
    const result = await pool.query(query, values);
    return result.rows[0];
  },

  upsertModelWeights: async (modelData) => {
    const result = await pool.query(`
      INSERT INTO ml_model_weights
        (model_name, task, format, parameter_count, size_bytes, sha256, source_path, weights, metadata)
      VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9::jsonb)
      ON CONFLICT (model_name) DO UPDATE SET
        task = EXCLUDED.task,
        format = EXCLUDED.format,
        parameter_count = EXCLUDED.parameter_count,
        size_bytes = EXCLUDED.size_bytes,
        sha256 = EXCLUDED.sha256,
        source_path = EXCLUDED.source_path,
        weights = EXCLUDED.weights,
        metadata = EXCLUDED.metadata,
        updated_at = CURRENT_TIMESTAMP
      RETURNING id, model_name, task, format, parameter_count, size_bytes, sha256, source_path, metadata, updated_at
    `, [
      modelData.model_name,
      modelData.task,
      modelData.format,
      modelData.parameter_count,
      modelData.size_bytes,
      modelData.sha256,
      modelData.source_path,
      modelData.weights,
      JSON.stringify(modelData.metadata || {})
    ]);
    return result.rows[0];
  },

  getModelWeights: async () => {
    const result = await pool.query(`
      SELECT id, model_name, task, format, parameter_count, size_bytes, sha256, source_path, metadata, updated_at
      FROM ml_model_weights
      ORDER BY updated_at DESC
    `);
    return result.rows;
  },
  
  getAllPatients: async (limit = 100, offset = 0) => {
    const result = await pool.query(
      'SELECT * FROM patients ORDER BY scan_date DESC LIMIT $1 OFFSET $2',
      [limit, offset]
    );
    return result.rows;
  },
  
  getPatientById: async (patientId) => {
    const result = await pool.query('SELECT * FROM patients WHERE patient_id = $1', [patientId]);
    return result.rows[0];
  },
  
  // Health vitals operations
  createHealthVital: async (vitalData) => {
    const fields = Object.keys(vitalData);
    const values = Object.values(vitalData);
    const placeholders = values.map((_, i) => `$${i + 1}`).join(', ');
    
    const query = `
      INSERT INTO health_vitals (${fields.join(', ')})
      VALUES (${placeholders})
      RETURNING *
    `;
    
    const result = await pool.query(query, values);
    return result.rows[0];
  },
  
  getRecentVitals: async (patientId, limit = 100) => {
    const result = await pool.query(
      'SELECT * FROM health_vitals WHERE patient_id = $1 ORDER BY timestamp DESC LIMIT $2',
      [patientId, limit]
    );
    return result.rows;
  },
  
  // Real-time metrics operations
  createMetric: async (metricType, metricValue, unit, patientId = null) => {
    const result = await pool.query(
      'INSERT INTO realtime_metrics (metric_type, metric_value, unit, patient_id) VALUES ($1, $2, $3, $4) RETURNING *',
      [metricType, metricValue, unit, patientId]
    );
    return result.rows[0];
  },
  
  getRecentMetrics: async (limit = 50) => {
    const result = await pool.query(
      'SELECT * FROM realtime_metrics ORDER BY timestamp DESC LIMIT $1',
      [limit]
    );
    return result.rows;
  },
  
  getMetricsByType: async (metricType, limit = 50) => {
    const result = await pool.query(
      'SELECT * FROM realtime_metrics WHERE metric_type = $1 ORDER BY timestamp DESC LIMIT $2',
      [metricType, limit]
    );
    return result.rows;
  },
  
  // Analytics operations
  getPatientStats: async () => {
    const result = await pool.query(`
      SELECT 
        COUNT(*) as total_patients,
        COUNT(CASE WHEN dl_classification = 'ABNORMAL' THEN 1 END) as abnormal_cases,
        COUNT(CASE WHEN risk_category = 'HIGH' THEN 1 END) as high_risk,
        AVG(age) as avg_age,
        AVG(risk_score) as avg_risk_score
      FROM patients
    `);
    return result.rows[0];
  },
  
  getRiskDistribution: async () => {
    const result = await pool.query(`
      SELECT risk_category, COUNT(*) as count
      FROM patients
      WHERE risk_category IS NOT NULL
      GROUP BY risk_category
    `);
    return result.rows;
  },
  
  getGenderDistribution: async () => {
    const result = await pool.query(`
      SELECT sex as gender, COUNT(*) as count
      FROM patients
      WHERE sex IS NOT NULL
      GROUP BY sex
    `);
    return result.rows;
  }
};

module.exports = { db, initializeDatabase, pool };
