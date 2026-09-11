'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { db, initializeDatabase, pool } = require('../config/postgres');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const EDGE_DIR = path.join(ROOT, 'karnix_spine_edge');
const RECORDS_PATH = path.join(EDGE_DIR, 'data', 'spine_records.json');
const MODELS_DIR = path.join(EDGE_DIR, 'models');

function normalizePercent(value) {
  const number = Number(value || 0);
  return number > 0 && number <= 1 ? number * 100 : number;
}

function nullableNumber(value) {
  const number = Number(value);
  return Number.isFinite(number) ? number : null;
}

function inferTask(fileName) {
  const lower = fileName.toLowerCase();
  if (lower.includes('mura')) return 'MURA';
  if (lower.includes('mri') || lower.includes('lumbar')) return 'LUMBAR_MRI';
  if (lower.includes('osteo') || lower.includes('oa')) return 'OSTEOARTHRITIS';
  if (lower.includes('spine')) return 'SPINE_SCREENING';
  return 'UNKNOWN';
}

async function importRecords() {
  if (!fs.existsSync(RECORDS_PATH)) return { imported: 0, source: RECORDS_PATH };
  const rows = JSON.parse(fs.readFileSync(RECORDS_PATH, 'utf8'));
  let imported = 0;
  for (const row of rows) {
    if (!row.patient_id) continue;
    const patientData = {
      patient_id: row.patient_id,
      age: nullableNumber(row.age),
      sex: row.sex && row.sex !== 'N/A' ? String(row.sex).toUpperCase() : 'U',
      abnormal_prob: normalizePercent(row.abnormal_prob),
      dl_classification: row.classification || 'Unknown',
      risk_score: nullableNumber(row.risk_score),
      risk_category: row.risk_category || (normalizePercent(row.abnormal_prob) >= 75 ? 'HIGH' : normalizePercent(row.abnormal_prob) >= 45 ? 'MODERATE' : 'LOW'),
      scan_date: row.date || null,
      image_path: row.image_path || null,
      modality: row.modality || 'XRAY',
      trabecular_bone_score: nullableNumber(row.trabecular_bone_score),
      cortical_thinning_index: nullableNumber(row.cortical_thinning_index),
      marrow_dark_ratio: nullableNumber(row.marrow_dark_ratio),
      marrow_signal: row.marrow_signal || null,
    };
    Object.keys(patientData).forEach((key) => patientData[key] == null && delete patientData[key]);
    await db.upsertPatient(patientData);
    imported += 1;
  }
  return { imported, source: RECORDS_PATH };
}

async function importWeights() {
  if (!fs.existsSync(MODELS_DIR)) return [];
  const files = fs.readdirSync(MODELS_DIR).filter((name) => name.endsWith('.weights'));
  const imported = [];
  for (const fileName of files) {
    const sourcePath = path.join(MODELS_DIR, fileName);
    const weights = fs.readFileSync(sourcePath);
    const stats = fs.statSync(sourcePath);
    const sha256 = crypto.createHash('sha256').update(weights).digest('hex');
    const row = await db.upsertModelWeights({
      model_name: fileName.replace(/\.weights$/i, ''),
      task: inferTask(fileName),
      format: 'KARNIX_SPINE_WEIGHTS',
      parameter_count: null,
      size_bytes: stats.size,
      sha256,
      source_path: sourcePath,
      weights,
      metadata: {
        importedAt: new Date().toISOString(),
        fileName,
        relativePath: path.relative(ROOT, sourcePath),
      },
    });
    imported.push(row);
  }
  return imported;
}

(async () => {
  try {
    await initializeDatabase();
    const records = await importRecords();
    const models = await importWeights();
    console.log(`[import] patient records: ${records.imported} from ${records.source}`);
    console.log(`[import] model weights: ${models.length}`);
    for (const model of models) {
      console.log(`[import] ${model.model_name} ${model.size_bytes} bytes sha256=${model.sha256}`);
    }
  } catch (error) {
    console.error('[import] failed:', error.message);
    process.exitCode = 1;
  } finally {
    await pool.end().catch(() => {});
  }
})();