use cdsl::{Context, DecisionTable, Engine, Pipeline, Rule, Scorecard, Status};

#[test]
fn test_rule_execution() {
    let engine = Engine::new().expect("Failed to create engine");
    let mut ctx = Context::new().expect("Failed to create context");

    ctx.set_int("age", 25).unwrap();
    ctx.set_float("salary", 6500.0).unwrap();
    ctx.set_bool("is_active", true).unwrap();
    ctx.set_string("name", "Alice").unwrap();

    assert_eq!(ctx.get_int("age", 0).unwrap(), 25);
    assert_eq!(ctx.get_float("salary", 0.0).unwrap(), 6500.0);
    assert!(ctx.get_bool("is_active", false).unwrap());
    assert_eq!(ctx.get_string("name", "").unwrap(), "Alice");

    let rule = Rule::parse("RULE test_r { WHEN age >= 18 && is_active == true THEN approve() }")
        .expect("Failed to parse rule");

    let report = engine.execute(&rule, &mut ctx).expect("Execution failed");
    assert_eq!(report.rule_name, "test_r");
    assert_eq!(report.status, Status::Passed);
}

#[test]
fn test_decision_table() {
    let dsl = r#"
        DECISION_TABLE risk_matrix
            INPUTS (credit: INT, score: FLOAT)
            OUTPUTS (approved: BOOL, rate: FLOAT)
            HIT_POLICY: FIRST
            ROW: credit > 600, score >= 80.0 -> true, 0.05
            DEFAULT: false, 0.20
        END
    "#;
    let dt = DecisionTable::parse(dsl).expect("Failed to parse decision table");
    let mut ctx = Context::new().expect("Failed to create context");
    ctx.set_int("credit", 720).unwrap();
    ctx.set_float("score", 95.0).unwrap();

    let rpt = dt.evaluate(&mut ctx).expect("Evaluation failed");
    assert!(rpt.matched);
    assert_eq!(rpt.table_name, "risk_matrix");
}

#[test]
fn test_scorecard() {
    let dsl = r#"
        SCORECARD credit_fast
            BASELINE: 500
            CHARACTERISTIC age:
                CASE < 25: -30
                CASE [25..50]: +40
                DEFAULT: +10
            OUTPUT: final_score
        END
    "#;
    let sc = Scorecard::parse(dsl).expect("Failed to parse scorecard");
    let mut ctx = Context::new().expect("Failed to create context");
    ctx.set_int("age", 30).unwrap();

    let rpt = sc.evaluate(&mut ctx).expect("Evaluation failed");
    assert_eq!(rpt.final_score, 540.0);
    assert_eq!(ctx.get_float("final_score", 0.0).unwrap(), 540.0);
}

#[test]
fn test_pipeline() {
    let engine = Engine::new().expect("Failed to create engine");
    let dsl = r#"
        PIPELINE approval_pipeline
            STAGE stage1:
                ON_FAIL: ABORT
            STAGE stage2:
                DEPENDS_ON: stage1
                ON_FAIL: CONTINUE
        END
    "#;
    let pipe = Pipeline::parse(dsl).expect("Failed to parse pipeline");
    let mut ctx = Context::new().expect("Failed to create context");

    let rpt = pipe.execute(&engine, &mut ctx).expect("Pipeline execution failed");
    assert_eq!(rpt.pipeline_name, "approval_pipeline");
    assert_eq!(rpt.num_stages, 2);
}
