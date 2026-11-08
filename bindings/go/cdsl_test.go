package cdsl_test

import (
	"testing"

	"github.com/quintin-lee/cdsl/bindings/go"
)

func TestEngineRuleEvaluation(t *testing.T) {
	engine, err := cdsl.NewEngine()
	if err != nil {
		t.Fatalf("Failed to create engine: %v", err)
	}
	defer engine.Close()

	ctx, err := cdsl.NewContext()
	if err != nil {
		t.Fatalf("Failed to create context: %v", err)
	}
	defer ctx.Close()

	ctx.SetInt("age", 25)
	ctx.SetFloat("salary", 8000.50)
	ctx.SetBool("is_vip", true)
	ctx.SetString("tier", "gold")

	if ctx.GetInt("age", 0) != 25 {
		t.Errorf("Expected age 25, got %d", ctx.GetInt("age", 0))
	}
	if ctx.GetFloat("salary", 0) != 8000.50 {
		t.Errorf("Expected salary 8000.50, got %f", ctx.GetFloat("salary", 0))
	}
	if !ctx.GetBool("is_vip", false) {
		t.Errorf("Expected is_vip true")
	}
	if ctx.GetString("tier", "") != "gold" {
		t.Errorf("Expected tier gold, got %s", ctx.GetString("tier", ""))
	}

	ruleDsl := `RULE check_vip {
		WHEN is_vip == true && salary >= 5000.0
		THEN approve()
	}`
	rule, err := cdsl.ParseRule(ruleDsl)
	if err != nil {
		t.Fatalf("Failed to parse rule: %v", err)
	}
	defer rule.Close()

	report, err := engine.Execute(rule, ctx)
	if err != nil {
		t.Fatalf("Rule execution failed: %v", err)
	}

	if report.RuleName != "check_vip" {
		t.Errorf("Expected rule name 'check_vip', got %s", report.RuleName)
	}
}

func TestDecisionTable(t *testing.T) {
	dtableDsl := `DECISION_TABLE risk_matrix
		INPUTS (score: INT, age: INT)
		OUTPUTS (status: BOOL, limit: FLOAT)
		HIT_POLICY: FIRST
		ROW: score >= 700, age >= 18 -> true, 50000.0
		ROW: score < 700, _ -> false, 0.0
		DEFAULT: false, 0.0
	END`

	dt, err := cdsl.ParseDecisionTable(dtableDsl)
	if err != nil {
		t.Fatalf("Failed to parse decision table: %v", err)
	}
	defer dt.Close()

	ctx, err := cdsl.NewContext()
	if err != nil {
		t.Fatalf("Failed to create context: %v", err)
	}
	defer ctx.Close()

	ctx.SetInt("score", 750)
	ctx.SetInt("age", 30)

	report, err := dt.Evaluate(ctx)
	if err != nil {
		t.Fatalf("Evaluation failed: %v", err)
	}

	if !report.Matched {
		t.Errorf("Expected table to match")
	}
	if report.TableName != "risk_matrix" {
		t.Errorf("Expected table name 'risk_matrix', got %s", report.TableName)
	}
}

func TestScorecard(t *testing.T) {
	scDsl := `SCORECARD credit_fast
		BASELINE: 500
		CHARACTERISTIC age:
			CASE < 25: -30
			CASE [25..50]: +40
			DEFAULT: +10
		CHARACTERISTIC income:
			CASE >= 10000: +50
			DEFAULT: 0
		OUTPUT: final_credit
	END`

	sc, err := cdsl.ParseScorecard(scDsl)
	if err != nil {
		t.Fatalf("Failed to parse scorecard: %v", err)
	}
	defer sc.Close()

	ctx, err := cdsl.NewContext()
	if err != nil {
		t.Fatalf("Failed to create context: %v", err)
	}
	defer ctx.Close()

	ctx.SetInt("age", 30)
	ctx.SetFloat("income", 12000.0)

	report, err := sc.Evaluate(ctx)
	if err != nil {
		t.Fatalf("Scorecard evaluation failed: %v", err)
	}

	// 500 + 40 + 50 = 590
	if report.FinalScore != 590.0 {
		t.Errorf("Expected final score 590, got %f", report.FinalScore)
	}
	if ctx.GetFloat("final_credit", 0.0) != 590.0 {
		t.Errorf("Expected context final_credit 590, got %f", ctx.GetFloat("final_credit", 0.0))
	}
}

func TestPipeline(t *testing.T) {
	engine, err := cdsl.NewEngine()
	if err != nil {
		t.Fatalf("Failed to create engine: %v", err)
	}
	defer engine.Close()

	pDsl := `PIPELINE loan_approval
		STAGE identity_check:
			ON_FAIL: ABORT
		STAGE scoring:
			DEPENDS_ON: identity_check
			ON_FAIL: CONTINUE
	END`

	p, err := cdsl.ParsePipeline(pDsl)
	if err != nil {
		t.Fatalf("Failed to parse pipeline: %v", err)
	}
	defer p.Close()

	ctx, err := cdsl.NewContext()
	if err != nil {
		t.Fatalf("Failed to create context: %v", err)
	}
	defer ctx.Close()

	rpt, err := p.Execute(engine, ctx)
	if err != nil {
		t.Fatalf("Pipeline execution failed: %v", err)
	}

	if rpt.PipelineName != "loan_approval" {
		t.Errorf("Expected pipeline name 'loan_approval', got %s", rpt.PipelineName)
	}
	if rpt.NumStages != 2 {
		t.Errorf("Expected 2 stages, got %d", rpt.NumStages)
	}
}
