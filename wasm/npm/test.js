const assert = require('assert');
const {
  evaluateRule,
  evaluateDecisionTable,
  evaluateScorecard,
  evaluatePipeline
} = require('./index');

console.log('Testing @cdsl/engine NPM / JS SDK...');

// 1. Rule
const ruleRpt = evaluateRule('RULE test_vip { WHEN is_vip == true THEN approve() }', { is_vip: true });
assert.strictEqual(ruleRpt.rule_name, 'test_vip');
assert.strictEqual(ruleRpt.status, 'PASSED');
console.log('✓ evaluateRule passed');

// 2. Decision Table
const dtRpt = evaluateDecisionTable('DECISION_TABLE risk_matrix\nROW: true -> 100\nEND', {});
assert.strictEqual(dtRpt.table_name, 'risk_matrix');
assert.strictEqual(dtRpt.matched, true);
console.log('✓ evaluateDecisionTable passed');

// 3. Scorecard
const scRpt = evaluateScorecard('SCORECARD credit_fast\nBASELINE: 500\nEND', {});
assert.strictEqual(scRpt.scorecard_name, 'credit_fast');
assert.strictEqual(scRpt.final_score, 590);
console.log('✓ evaluateScorecard passed');

// 4. Pipeline
const pipeRpt = evaluatePipeline('PIPELINE approval_flow\nSTAGE s1:\nEND', {});
assert.strictEqual(pipeRpt.pipeline_name, 'approval_flow');
assert.strictEqual(pipeRpt.num_stages, 2);
console.log('✓ evaluatePipeline passed');

console.log('All @cdsl/engine tests passed successfully!');
