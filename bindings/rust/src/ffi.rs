//! Raw C-FFI bindings to libcdsl.

use std::os::raw::{c_char, c_double, c_int, c_void};

pub enum CdslVm {}
pub enum CdslContext {}
pub enum CdslRule {}
pub enum CdslDtable {}
pub enum CdslScorecard {}
pub enum CdslPipeline {}
pub enum CdslSchema {}
pub enum CdslErrorList {}

#[repr(C)]
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum CdslStatus {
    Passed = 0,
    PartiallyPassed = 1,
    Failed = 2,
    Error = 3,
}

#[repr(C)]
pub struct CdslRuleReport {
    pub rule_name: *mut c_char,
    pub status: CdslStatus,
    pub total_obtained_score: c_int,
    pub total_max_score: c_int,
    pub decision_summary: *mut c_char,
    pub metrics: *mut c_void,
    pub num_metrics: usize,
    pub execution_time_us: c_double,
}

#[repr(C)]
pub struct CdslDtableReport {
    pub table_name: *mut c_char,
    pub matched: bool,
    pub matches: *mut c_void,
    pub num_matches: usize,
    pub final_outputs: *mut c_void,
    pub num_outputs: usize,
    pub error_message: *mut c_char,
}

#[repr(C)]
pub struct CdslScorecardReport {
    pub scorecard_name: *mut c_char,
    pub baseline_score: c_double,
    pub final_score: c_double,
    pub contributions: *mut c_void,
    pub num_contributions: usize,
}

#[repr(C)]
pub struct CdslPipelineReport {
    pub pipeline_name: *mut c_char,
    pub status: c_int,
    pub stage_reports: *mut c_void,
    pub num_stages: usize,
    pub total_duration_us: c_double,
}

extern "C" {
    pub fn cdsl_vm_create(schema: *const CdslSchema) -> *mut CdslVm;
    pub fn cdsl_vm_free(vm: *mut CdslVm);
    pub fn cdsl_vm_execute(
        vm: *mut CdslVm,
        rule: *const CdslRule,
        ctx: *mut CdslContext,
    ) -> *mut CdslRuleReport;

    pub fn cdsl_parse_string(
        dsl_code: *const c_char,
        errors: *mut *mut CdslErrorList,
    ) -> *mut CdslRule;
    pub fn cdsl_free_rule(rule: *mut CdslRule);
    pub fn cdsl_report_free(report: *mut CdslRuleReport);

    pub fn cdsl_context_create(schema: *const CdslSchema) -> *mut CdslContext;
    pub fn cdsl_context_free(ctx: *mut CdslContext);
    pub fn cdsl_context_set_int(ctx: *mut CdslContext, name: *const c_char, val: c_int);
    pub fn cdsl_context_set_float(ctx: *mut CdslContext, name: *const c_char, val: c_double);
    pub fn cdsl_context_set_bool(ctx: *mut CdslContext, name: *const c_char, val: c_int);
    pub fn cdsl_context_set_string(
        ctx: *mut CdslContext,
        name: *const c_char,
        val: *const c_char,
    );
    pub fn cdsl_context_get_int(
        ctx: *const CdslContext,
        name: *const c_char,
        default_val: c_int,
    ) -> c_int;
    pub fn cdsl_context_get_float(
        ctx: *const CdslContext,
        name: *const c_char,
        default_val: c_double,
    ) -> c_double;
    pub fn cdsl_context_get_bool(
        ctx: *const CdslContext,
        name: *const c_char,
        default_val: c_int,
    ) -> c_int;
    pub fn cdsl_context_get_string(
        ctx: *const CdslContext,
        name: *const c_char,
        default_val: *const c_char,
    ) -> *const c_char;

    pub fn cdsl_dtable_parse_string(
        dsl: *const c_char,
        errors: *mut CdslErrorList,
    ) -> *mut CdslDtable;
    pub fn cdsl_dtable_free(dt: *mut CdslDtable);
    pub fn cdsl_dtable_eval(
        dt: *const CdslDtable,
        ctx: *mut CdslContext,
    ) -> *mut CdslDtableReport;
    pub fn cdsl_dtable_report_free(rpt: *mut CdslDtableReport);

    pub fn cdsl_scorecard_parse_string(
        dsl: *const c_char,
        errors: *mut CdslErrorList,
    ) -> *mut CdslScorecard;
    pub fn cdsl_scorecard_free(sc: *mut CdslScorecard);
    pub fn cdsl_scorecard_eval(
        sc: *const CdslScorecard,
        ctx: *mut CdslContext,
    ) -> *mut CdslScorecardReport;
    pub fn cdsl_scorecard_report_free(rpt: *mut CdslScorecardReport);

    pub fn cdsl_pipeline_parse_string(
        dsl: *const c_char,
        errors: *mut CdslErrorList,
    ) -> *mut CdslPipeline;
    pub fn cdsl_pipeline_free(p: *mut CdslPipeline);
    pub fn cdsl_pipeline_execute(
        p: *const CdslPipeline,
        vm: *mut CdslVm,
        ctx: *mut CdslContext,
    ) -> *mut CdslPipelineReport;
    pub fn cdsl_pipeline_report_free(rpt: *mut CdslPipelineReport);
}
