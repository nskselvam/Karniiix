/**
 * KarnixOS – PostgreSQL → C Header Exporter
 *
 * Reads the backend .env, connects to PostgreSQL, exports ANONYMISED patient
 * data (patient_id code only – NO names, NO usernames, NO doctor PII) and
 * generates  db_data.h  which the kernel includes at compile time.
 *
 * Privacy rules enforced here:
 *   • Only patient_id (coded, e.g. "PT-014") is exported – zero real names.
 *   • Doctor/user identifiers are stripped entirely.
 *   • Age, sex, dates, clinical scores are retained (needed for medical AI).
 *
 * Usage:
 *   node db_exporter.js [--limit N]   (default 50)
 */

'use strict';

const path   = require('path');
const fs     = require('fs');
const { Pool } = require('pg');

/* ── Load .env from the backend directory ─────────────────────────────── */
const ENV_PATH = path.resolve(__dirname,
    '../../spine_analytics_dashboard/backend/.env');

function loadEnv(filePath) {
    try {
        const lines = fs.readFileSync(filePath, 'utf8').split('\n');
        const env = {};
        for (const line of lines) {
            const m = line.match(/^\s*([A-Z_][A-Z0-9_]*)\s*=\s*(.*)\s*$/);
            if (m) env[m[1]] = m[2].replace(/^["']|["']$/g, '').trim();
        }
        return env;
    } catch (e) {
        console.error('[!] Cannot read .env at', filePath, '–', e.message);
        process.exit(1);
    }
}

const env = loadEnv(ENV_PATH);
const limit = (() => {
    const idx = process.argv.indexOf('--limit');
    return idx !== -1 ? parseInt(process.argv[idx + 1]) || 50 : 50;
})();

const pool = new Pool({
    host:     env.DB_HOST     || 'localhost',
    port:     parseInt(env.DB_PORT) || 5432,
    user:     env.DB_USER     || 'postgres',
    password: env.DB_PASSWORD || '',
    database: env.DB_NAME     || 'postgres',
    connectionTimeoutMillis: 5000,
});

/* ── C string escaping ─────────────────────────────────────────────────── */
function cstr(s) {
    if (s == null) return '""';
    return '"' + String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';
}

function clamp(v, lo, hi) {
    v = parseFloat(v) || 0;
    return Math.max(lo, Math.min(hi, v));
}

function fmtDate(d) {
    if (!d) return '2026-01-01';
    const dd = new Date(d);
    if (isNaN(dd)) return String(d).substring(0, 10);
    return dd.toISOString().substring(0, 10);
}

function derivedTbs(p) {
    if (p.trabecular_bone_score) return clamp(p.trabecular_bone_score, 0, 3);
    const bone = clamp(p.bone_density || 0, -4, 2);
    const disc = clamp(p.disc_grade || 0, 0, 5);
    return clamp(1.35 + bone * 0.08 - disc * 0.03, 0.4, 1.8);
}

function derivedCorticalThinning(p) {
    if (p.cortical_thinning_index) return clamp(p.cortical_thinning_index, 0, 100);
    const age = clamp(p.age || 0, 0, 100);
    const bone = clamp(-(p.bone_density || 0), 0, 4);
    return clamp(age * 0.35 + bone * 18 + (p.frac_grade || 0) * 10, 0, 100);
}

function derivedMarrowDark(p) {
    if (p.marrow_dark_ratio) return clamp(p.marrow_dark_ratio, 0, 100);
    return clamp((p.abnormal_prob || 0) * 0.45 + (p.disc_grade || 0) * 6 + (p.frac_grade || 0) * 7, 0, 100);
}

/* ── Query helpers ─────────────────────────────────────────────────────── */
async function fetchPatients(client) {
    /* Select ONLY anonymised clinical fields – no names, no doctor IDs */
    const q = `
        SELECT
            patient_id,
            COALESCE(age, 0)                         AS age,
            UPPER(COALESCE(sex, 'U'))                AS sex,
            TO_CHAR(scan_date, 'YYYY-MM-DD')         AS scan_date,
            COALESCE(risk_score, 50)                 AS risk_score,
            COALESCE(abnormal_prob, 50)              AS abnormal_prob,
            COALESCE(dl_classification, 'Unknown')   AS classification,
            COALESCE(risk_category, 'MODERATE')      AS risk_category,
            COALESCE(cobb_angle, 0)                  AS cobb_angle,
            COALESCE(bone_density_tscore, 0)         AS bone_density,
            COALESCE(compression_fracture_grade, 0)  AS frac_grade,
            COALESCE(disc_degeneration_grade, 0)     AS disc_grade,
            COALESCE(pelvic_incidence, 0)            AS pi,
            COALESCE(pelvic_tilt, 0)                 AS pt,
            COALESCE(lumbar_lordosis_angle, 0)       AS ll,
            COALESCE(sacral_slope, 0)                AS ss,
            COALESCE(pelvic_radius, 100)             AS pr,
            COALESCE(grade_of_spondylolisthesis, 0)  AS sp
        FROM patients
        ORDER BY scan_date DESC NULLS LAST
        LIMIT $1`;
    const res = await client.query(q, [limit]);
    return res.rows;
}

async function fetchAggregates(client) {
    const q = `
        SELECT
            COUNT(*)                                         AS total,
            COUNT(*) FILTER (WHERE risk_category='HIGH')    AS high_risk,
            COUNT(*) FILTER (WHERE risk_category='MODERATE') AS mod_risk,
            COUNT(*) FILTER (WHERE risk_category='LOW')     AS low_risk,
            ROUND(AVG(risk_score)::numeric, 1)              AS avg_risk,
            COUNT(*) FILTER (WHERE dl_classification='Abnormal')           AS abnormal,
            COUNT(*) FILTER (WHERE dl_classification='Normal')             AS normal,
            COUNT(*) FILTER (WHERE dl_classification ILIKE '%spondyl%')    AS spondyl,
            COUNT(*) FILTER (WHERE dl_classification ILIKE '%disk%'
                               OR  dl_classification ILIKE '%disc%')       AS disk_hernia
        FROM patients`;
    const r = await client.query(q);
    return r.rows[0];
}

/* UCI vertebral from patients table (if fully populated) */
async function fetchUCI(client) {
    const q = `
        SELECT
            COALESCE(pelvic_incidence, 60)           AS pi,
            COALESCE(pelvic_tilt, 20)                AS pt,
            COALESCE(lumbar_lordosis_angle, 40)      AS ll,
            COALESCE(sacral_slope, 30)               AS ss,
            COALESCE(pelvic_radius, 110)             AS pr,
            COALESCE(grade_of_spondylolisthesis, 0)  AS sp,
            COALESCE(dl_classification, 'Normal')    AS label
        FROM patients
        WHERE pelvic_incidence IS NOT NULL
        LIMIT 10`;
    const r = await client.query(q);
    return r.rows;
}

/* ── Fallback data when DB is empty ──────────────────────────────────── */
const FALLBACK_PATIENTS = [
    {patient_id:'PT-001',age:45,sex:'M',scan_date:'2026-02-26',risk_score:57.7,abnormal_prob:99,classification:'Abnormal',risk_category:'HIGH',cobb_angle:18,bone_density:-1.2,frac_grade:1,disc_grade:2,pi:63,pt:22,ll:39,ss:40,pr:98,sp:-0.2},
    {patient_id:'PT-002',age:32,sex:'F',scan_date:'2026-02-26',risk_score:100,abnormal_prob:0,classification:'Normal',risk_category:'LOW',cobb_angle:4,bone_density:0.5,frac_grade:0,disc_grade:0,pi:39,pt:10,ll:25,ss:29,pr:114,sp:4.5},
    {patient_id:'PT-003',age:58,sex:'M',scan_date:'2026-02-15',risk_score:42.1,abnormal_prob:87,classification:'Abnormal',risk_category:'HIGH',cobb_angle:24,bone_density:-2.1,frac_grade:2,disc_grade:3,pi:68,pt:22,ll:50,ss:46,pr:105,sp:-3.5},
    {patient_id:'PT-004',age:29,sex:'F',scan_date:'2026-02-20',risk_score:93.4,abnormal_prob:6,classification:'Normal',risk_category:'LOW',cobb_angle:3,bone_density:1.1,frac_grade:0,disc_grade:0,pi:37,pt:11,ll:23,ss:25,pr:113,sp:21.6},
    {patient_id:'PT-005',age:67,sex:'M',scan_date:'2026-02-22',risk_score:12.3,abnormal_prob:76,classification:'DiskHernia',risk_category:'HIGH',cobb_angle:8,bone_density:-2.8,frac_grade:1,disc_grade:4,pi:77,pt:28,ll:49,ss:48,pr:103,sp:6.5},
    {patient_id:'PT-006',age:41,sex:'F',scan_date:'2026-02-23',risk_score:89.0,abnormal_prob:11,classification:'Normal',risk_category:'LOW',cobb_angle:5,bone_density:0.2,frac_grade:0,disc_grade:1,pi:50,pt:18,ll:27,ss:32,pr:112,sp:0},
    {patient_id:'PT-007',age:53,sex:'M',scan_date:'2026-02-24',risk_score:31.0,abnormal_prob:91,classification:'Spondylolisthesis',risk_category:'HIGH',cobb_angle:12,bone_density:-1.8,frac_grade:2,disc_grade:3,pi:65,pt:23,ll:58,ss:41,pr:124,sp:52.2},
    {patient_id:'PT-008',age:36,sex:'F',scan_date:'2026-02-25',risk_score:98.5,abnormal_prob:1,classification:'Normal',risk_category:'LOW',cobb_angle:2,bone_density:0.8,frac_grade:0,disc_grade:0,pi:40,pt:13,ll:31,ss:30,pr:127,sp:32.1},
    {patient_id:'PT-009',age:72,sex:'M',scan_date:'2026-02-26',risk_score:8.9,abnormal_prob:96,classification:'Abnormal',risk_category:'HIGH',cobb_angle:31,bone_density:-3.1,frac_grade:3,disc_grade:4,pi:70,pt:28,ll:49,ss:41,pr:119,sp:65.4},
    {patient_id:'PT-010',age:44,sex:'F',scan_date:'2026-02-26',risk_score:75.6,abnormal_prob:24,classification:'DiskHernia',risk_category:'MODERATE',cobb_angle:9,bone_density:-0.5,frac_grade:1,disc_grade:2,pi:50,pt:18,ll:27,ss:32,pr:112,sp:0},
    {patient_id:'PT-011',age:38,sex:'M',scan_date:'2026-02-26',risk_score:92.1,abnormal_prob:7,classification:'Normal',risk_category:'LOW',cobb_angle:4,bone_density:0.6,frac_grade:0,disc_grade:0,pi:38,pt:10,ll:25,ss:29,pr:114,sp:4.5},
    {patient_id:'PT-012',age:61,sex:'F',scan_date:'2026-02-26',risk_score:20.4,abnormal_prob:84,classification:'Spondylolisthesis',risk_category:'HIGH',cobb_angle:19,bone_density:-2.4,frac_grade:2,disc_grade:3,pi:42,pt:14,ll:33,ss:28,pr:116,sp:21.6},
    {patient_id:'PT-013',age:27,sex:'M',scan_date:'2026-02-27',risk_score:87.6,abnormal_prob:12,classification:'Normal',risk_category:'LOW',cobb_angle:3,bone_density:1.2,frac_grade:0,disc_grade:0,pi:39,pt:10,ll:25,ss:29,pr:113,sp:4.5},
    {patient_id:'PT-014',age:55,sex:'F',scan_date:'2026-02-27',risk_score:44.8,abnormal_prob:69,classification:'DiskHernia',risk_category:'MODERATE',cobb_angle:11,bone_density:-1.0,frac_grade:1,disc_grade:3,pi:65,pt:23,ll:58,ss:41,pr:124,sp:0},
    {patient_id:'PT-015',age:48,sex:'M',scan_date:'2026-02-27',risk_score:61.1,abnormal_prob:38,classification:'DiskHernia',risk_category:'MODERATE',cobb_angle:7,bone_density:-0.8,frac_grade:1,disc_grade:2,pi:60,pt:22,ll:39,ss:40,pr:98,sp:0},
    {patient_id:'PT-016',age:33,sex:'F',scan_date:'2026-02-27',risk_score:99.9,abnormal_prob:0,classification:'Normal',risk_category:'LOW',cobb_angle:2,bone_density:1.0,frac_grade:0,disc_grade:0,pi:38,pt:10,ll:25,ss:29,pr:114,sp:4.5},
    {patient_id:'PT-017',age:76,sex:'M',scan_date:'2026-02-28',risk_score:5.5,abnormal_prob:98,classification:'Abnormal',risk_category:'HIGH',cobb_angle:36,bone_density:-3.5,frac_grade:3,disc_grade:4,pi:71,pt:28,ll:50,ss:48,pr:116,sp:76.8},
    {patient_id:'PT-018',age:42,sex:'F',scan_date:'2026-02-28',risk_score:78.0,abnormal_prob:21,classification:'Normal',risk_category:'LOW',cobb_angle:5,bone_density:0.3,frac_grade:0,disc_grade:1,pi:40,pt:13,ll:31,ss:30,pr:127,sp:0},
    {patient_id:'PT-019',age:59,sex:'M',scan_date:'2026-02-28',risk_score:31.5,abnormal_prob:88,classification:'Spondylolisthesis',risk_category:'HIGH',cobb_angle:14,bone_density:-1.5,frac_grade:2,disc_grade:3,pi:65,pt:23,ll:58,ss:41,pr:124,sp:52.2},
    {patient_id:'PT-020',age:31,sex:'F',scan_date:'2026-02-28',risk_score:94.5,abnormal_prob:5,classification:'Normal',risk_category:'LOW',cobb_angle:3,bone_density:0.9,frac_grade:0,disc_grade:0,pi:41,pt:13,ll:31,ss:30,pr:131,sp:76.8},
];

const FALLBACK_AGGREGATES = {
    total:9999, high_risk:6, mod_risk:4, low_risk:10,
    avg_risk:54.2, abnormal:4, normal:10, spondyl:3, disk_hernia:3,
    _fallback: true,
};

const FALLBACK_UCI = [
    {pi:63.0,pt:22.5,ll:39.6,ss:40.4,pr:98.6,sp:-0.2,label:'Normal'},
    {pi:39.0,pt:10.0,ll:25.0,ss:29.0,pr:114.4,sp:4.5,label:'Normal'},
    {pi:68.8,pt:22.2,ll:50.0,ss:46.6,pr:105.9,sp:-3.5,label:'Normal'},
    {pi:37.7,pt:11.9,ll:23.3,ss:25.7,pr:113.0,sp:21.6,label:'DiskHernia'},
    {pi:77.1,pt:28.7,ll:49.8,ss:48.4,pr:103.0,sp:6.5,label:'DiskHernia'},
    {pi:88.0,pt:39.0,ll:81.7,ss:48.9,pr:116.6,sp:56.7,label:'Spondylolisthesis'},
    {pi:65.6,pt:23.6,ll:58.2,ss:41.9,pr:124.0,sp:52.2,label:'Spondylolisthesis'},
    {pi:50.8,pt:18.5,ll:27.2,ss:32.2,pr:112.7,sp:32.1,label:'DiskHernia'},
    {pi:70.1,pt:28.4,ll:49.2,ss:41.6,pr:119.5,sp:65.4,label:'Spondylolisthesis'},
    {pi:40.7,pt:13.5,ll:31.1,ss:30.2,pr:131.2,sp:76.8,label:'Spondylolisthesis'},
];

/* ── Header generator ─────────────────────────────────────────────────── */
function generateHeader(patients, uci, agg, source) {
    const now = new Date().toISOString().replace('T', ' ').substring(0, 19);
    const lines = [];

    lines.push('/* AUTO-GENERATED by db_exporter.js – DO NOT EDIT MANUALLY */',
               `/* Source : ${source} */`,
               `/* Generated: ${now} UTC */`,
               `/* Privacy  : patient_id codes only – no names, no doctor PII */`,
               '',
               '#ifndef DB_DATA_H',
               '#define DB_DATA_H',
               '');

    /* ── Aggregate statistics (for `stats` command) ── */
    lines.push('/* ── Database aggregate statistics ─────────────────────── */');
    lines.push(`#define DB_TOTAL_PATIENTS  ${agg.total}`);
    lines.push(`#define DB_HIGH_RISK       ${agg.high_risk}`);
    lines.push(`#define DB_MOD_RISK        ${agg.mod_risk}`);
    lines.push(`#define DB_LOW_RISK        ${agg.low_risk}`);
    lines.push(`#define DB_AVG_RISK_X10    ${Math.round(parseFloat(agg.avg_risk||50)*10)}`);
    lines.push(`#define DB_CNT_ABNORMAL    ${agg.abnormal}`);
    lines.push(`#define DB_CNT_NORMAL      ${agg.normal}`);
    lines.push(`#define DB_CNT_SPONDYL     ${agg.spondyl}`);
    lines.push(`#define DB_CNT_DISK        ${agg.disk_hernia}`);
    lines.push(`#define DB_SOURCE          ${cstr(source)}`);
    lines.push('');

    /* ── SpineRec typedef (extended with clinical fields) ── */
    lines.push('/* ── Patient record (anonymised) ───────────────────────── */');
    lines.push('typedef struct {');
    lines.push('    const char *patient_id;    /* e.g. "PT-014" – coded, no real name */');
    lines.push('    int         age;');
    lines.push('    const char *sex;           /* "M" | "F" | "U" */');
    lines.push('    const char *date;          /* YYYY-MM-DD */');
    lines.push('    int         risk_score_int; /* risk_score * 10 */');
    lines.push('    int         abnorm_pct;     /* abnormal_prob * 100 / 100 as int% */');
    lines.push('    const char *classification; /* DL result */');
    lines.push('    const char *risk_category;  /* HIGH / MODERATE / LOW */');
    lines.push('    int         cobb_x10;       /* Cobb angle * 10 */');
    lines.push('    int         bone_x10;       /* bone density T-score * 10 (signed) */');
    lines.push('    int         frac_grade;     /* compression fracture grade 0-3 */');
    lines.push('    int         disc_grade;     /* disc degeneration 0-4 */');
    lines.push('    int         tbs_x100;       /* DEXA TBS texture proxy * 100 */');
    lines.push('    int         cortical_idx;   /* CT cortical thinning index 0-100 */');
    lines.push('    int         marrow_dark_pct;/* MRI dark marrow ratio 0-100 */');
    lines.push('    const char *marrow_signal;  /* bright/dark marrow summary */');
    lines.push('} SpineRec;');
    lines.push('');

    lines.push(`#define NRECORDS ${patients.length}`);
    lines.push('static const SpineRec g_records[NRECORDS] = {');
    for (const p of patients) {
        const rs = Math.round(clamp(p.risk_score, 0, 999.9) * 10);
        const ap = Math.round(clamp(p.abnormal_prob, 0, 100));
        const cb = Math.round(clamp(p.cobb_angle || p.cobb_x10 || 0, -999, 999) * 10);
        const bd = Math.round(clamp(p.bone_density || (p.bone_x10 / 10) || 0, -99, 99) * 10);
        const fg = parseInt(p.frac_grade) || 0;
        const dg = parseInt(p.disc_grade) || 0;
        const tbs = Math.round(derivedTbs(p) * 100);
        const cortical = Math.round(derivedCorticalThinning(p));
        const marrow = Math.round(derivedMarrowDark(p));
        const marrowSignal = p.marrow_signal || (marrow >= 45 ? 'Dark marrow signal' : 'Bright marrow signal');
        lines.push(
            `    {${cstr(p.patient_id)}, ${p.age}, ${cstr(p.sex)}, ` +
            `${cstr(fmtDate(p.scan_date))}, ` +
            `${rs}, ${ap}, ` +
            `${cstr(p.classification)}, ${cstr((p.risk_category||'MODERATE').toUpperCase())}, ` +
            `${cb}, ${bd}, ${fg}, ${dg}, ${tbs}, ${cortical}, ${marrow}, ${cstr(marrowSignal)}},`);
    }
    lines.push('};');
    lines.push('');

    /* ── UCI records ── */
    lines.push('/* ── UCI Vertebral column records ──────────────────────── */');
    lines.push('typedef struct {');
    lines.push('    int pi10, pt10, ll10;');
    lines.push('    int ss10, pr10, sp10;');
    lines.push('    const char *label;');
    lines.push('} UCIRecord;');
    lines.push('');
    lines.push(`#define NUCI ${uci.length}`);
    lines.push('static const UCIRecord g_uci[NUCI] = {');
    for (const u of uci) {
        const f = v => Math.round(clamp(v, -9999, 9999) * 10);
        lines.push(`    {${f(u.pi)}, ${f(u.pt)}, ${f(u.ll)}, ${f(u.ss)}, ${f(u.pr)}, ${f(u.sp)}, ${cstr(u.label)}},`);
    }
    lines.push('};');
    lines.push('');
    lines.push('#endif /* DB_DATA_H */');

    return lines.join('\n') + '\n';
}

/* ── Main ─────────────────────────────────────────────────────────────── */
(async () => {
    let patients, uci, agg, source;
    let client;

    try {
        client = await pool.connect();
        console.log('[db_exporter] Connected to PostgreSQL at',
                    `${env.DB_HOST}:${env.DB_PORT}/${env.DB_NAME}`);

        /* Verify patients table exists */
        const check = await client.query(`
            SELECT EXISTS (
                SELECT 1 FROM information_schema.tables
                WHERE table_name='patients'
            ) AS tbl_exists`);

        if (!check.rows[0].tbl_exists) {
            console.warn('[db_exporter] Table "patients" not found – using fallback data');
            throw new Error('no_table');
        }

        const countRes = await client.query('SELECT COUNT(*) FROM patients');
        const count = parseInt(countRes.rows[0].count);
        if (count === 0) {
            console.warn('[db_exporter] "patients" table is empty – using fallback data');
            throw new Error('empty_table');
        }

        console.log(`[db_exporter] Exporting ${Math.min(count, limit)} of ${count} records...`);
        patients = await fetchPatients(client);
        uci      = await fetchUCI(client);
        agg      = await fetchAggregates(client);
        source   = `PostgreSQL ${env.DB_HOST}:${env.DB_PORT}/${env.DB_NAME} (${count} total records)`;

        if (uci.length === 0) uci = FALLBACK_UCI;

    } catch (e) {
        if (e.message !== 'no_table' && e.message !== 'empty_table') {
            console.warn('[db_exporter] DB error:', e.message,
                         '– using fallback embedded data');
        }
        patients = FALLBACK_PATIENTS;
        uci      = FALLBACK_UCI;
        agg      = FALLBACK_AGGREGATES;
        source   = 'FALLBACK (hardcoded – DB unavailable)';
    } finally {
        if (client) client.release();
        pool.end().catch(() => {});
    }

    const outPath = path.resolve(__dirname, 'db_data.h');
    const header  = generateHeader(patients, uci, agg, source);
    fs.writeFileSync(outPath, header, 'utf8');
    console.log(`[db_exporter] Written: ${outPath}`);
    console.log(`[db_exporter] Records : ${patients.length} patients, ${uci.length} UCI`);
    console.log(`[db_exporter] Source  : ${source}`);
    console.log('[db_exporter] Privacy : patient_id codes only – no names exported');
})();
