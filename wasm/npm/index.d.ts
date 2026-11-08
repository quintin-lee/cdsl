export interface RuleReport {
  rule_name: string;
  status: string;
  total_obtained_score?: number;
  total_max_score?: number;
  decision_summary?: string;
  error?: string;
}

export interface DecisionTableReport {
  table_name: string;
  matched: boolean;
  num_matches: number;
  error?: string;
}

export interface ScorecardReport {
  scorecard_name: string;
  baseline_score: number;
  final_score: number;
  error?: string;
}

export interface PipelineReport {
  pipeline_name: string;
  status: number;
  num_stages: number;
  duration_us: number;
  error?: string;
}

export function evaluateRule(dsl: string, context?: Record<string, any>): RuleReport;
export function evaluateDecisionTable(dsl: string, context?: Record<string, any>): DecisionTableReport;
export function evaluateScorecard(dsl: string, context?: Record<string, any>): ScorecardReport;
export function evaluatePipeline(dsl: string, context?: Record<string, any>): PipelineReport;
