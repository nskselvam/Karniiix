const express = require('express');
const { db } = require('../config/postgres');

const router = express.Router();

router.get('/', async (_req, res) => {
  try {
    const models = await db.getModelWeights();
    res.json({ success: true, count: models.length, data: models });
  } catch (error) {
    res.status(500).json({ success: false, message: 'Failed to retrieve model weights', error: error.message });
  }
});

router.get('/:name/download', async (req, res) => {
  try {
    const result = await db.query(
      'SELECT model_name, weights, sha256 FROM ml_model_weights WHERE model_name = $1',
      [req.params.name]
    );
    const model = result.rows[0];
    if (!model) {
      return res.status(404).json({ success: false, message: 'Model weights not found' });
    }
    res.setHeader('Content-Type', 'application/octet-stream');
    res.setHeader('Content-Disposition', `attachment; filename="${model.model_name}.weights"`);
    res.setHeader('X-Model-SHA256', model.sha256);
    res.send(model.weights);
  } catch (error) {
    res.status(500).json({ success: false, message: 'Failed to download model weights', error: error.message });
  }
});

module.exports = router;