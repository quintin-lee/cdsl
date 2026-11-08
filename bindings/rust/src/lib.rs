//! Safe, high-performance Rust bindings for CDSL (C Domain Specific Language).
//!
//! Provides idiomatic Rust wrappers with RAII memory management, thread safety,
//! and support for Rules, Decision Tables, Scorecards, and DAG Pipelines.

pub mod ffi;

use std::ffi::{CStr, CString};
use thiserror::Error;

#[derive(Error, Debug)]
pub enum CdslError {
    #[error("Failed to allocate CDSL engine")]
    EngineAllocationFailed,
    #[error("Failed to parse DSL code")]
    ParseError,
    #[error("Failed to allocate evaluation context")]
    ContextAllocationFailed,
    #[error("Evaluation returned null report")]
    EvaluationFailed,
    #[error("String contains null bytes: {0}")]
    NulError(#[from] std::ffi::NulError),
}

/// Execution status of a CDSL rule.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Status {
    Passed,
    PartiallyPassed,
    Failed,
    Error,
}

impl From<ffi::CdslStatus> for Status {
    fn from(s: ffi::CdslStatus) -> Self {
        match s {
            ffi::CdslStatus::Passed => Status::Passed,
            ffi::CdslStatus::PartiallyPassed => Status::PartiallyPassed,
            ffi::CdslStatus::Failed => Status::Failed,
            ffi::CdslStatus::Error => Status::Error,
        }
    }
}

/// Rule execution report.
#[derive(Debug, Clone)]
pub struct RuleReport {
    pub rule_name: String,
    pub status: Status,
    pub total_obtained_score: i32,
    pub total_max_score: i32,
    pub decision_summary: String,
    pub execution_time_us: f64,
}

/// Virtual Machine engine for evaluating CDSL rules and pipelines.
pub struct Engine {
    raw: *mut ffi::CdslVm,
}

unsafe impl Send for Engine {}
unsafe impl Sync for Engine {}

impl Engine {
    /// Creates a new CDSL virtual machine.
    pub fn new() -> Result<Self, CdslError> {
        let raw = unsafe { ffi::cdsl_vm_create(std::ptr::null()) };
        if raw.is_null() {
            return Err(CdslError::EngineAllocationFailed);
        }
        Ok(Self { raw })
    }

    /// Evaluates a rule against a context.
    pub fn execute(&self, rule: &Rule, ctx: &mut Context) -> Result<RuleReport, CdslError> {
        let raw_report = unsafe { ffi::cdsl_vm_execute(self.raw, rule.raw, ctx.raw) };
        if raw_report.is_null() {
            return Err(CdslError::EvaluationFailed);
        }

        let rpt = unsafe { &*raw_report };
        let rule_name = if !rpt.rule_name.is_null() {
            unsafe { CStr::from_ptr(rpt.rule_name).to_string_lossy().into_owned() }
        } else {
            String::new()
        };

        let decision_summary = if !rpt.decision_summary.is_null() {
            unsafe { CStr::from_ptr(rpt.decision_summary).to_string_lossy().into_owned() }
        } else {
            String::new()
        };

        let report = RuleReport {
            rule_name,
            status: rpt.status.into(),
            total_obtained_score: rpt.total_obtained_score,
            total_max_score: rpt.total_max_score,
            decision_summary,
            execution_time_us: rpt.execution_time_us,
        };

        unsafe { ffi::cdsl_report_free(raw_report) };
        Ok(report)
    }
}

impl Drop for Engine {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            unsafe { ffi::cdsl_vm_free(self.raw) };
            self.raw = std::ptr::null_mut();
        }
    }
}

/// Evaluation context storing input and output variables.
pub struct Context {
    raw: *mut ffi::CdslContext,
}

unsafe impl Send for Context {}

impl Context {
    /// Creates a new empty context.
    pub fn new() -> Result<Self, CdslError> {
        let raw = unsafe { ffi::cdsl_context_create(std::ptr::null()) };
        if raw.is_null() {
            return Err(CdslError::ContextAllocationFailed);
        }
        Ok(Self { raw })
    }

    pub fn set_int(&mut self, name: &str, val: i32) -> Result<(), CdslError> {
        let c_name = CString::new(name)?;
        unsafe { ffi::cdsl_context_set_int(self.raw, c_name.as_ptr(), val) };
        Ok(())
    }

    pub fn set_float(&mut self, name: &str, val: f64) -> Result<(), CdslError> {
        let c_name = CString::new(name)?;
        unsafe { ffi::cdsl_context_set_float(self.raw, c_name.as_ptr(), val) };
        Ok(())
    }

    pub fn set_bool(&mut self, name: &str, val: bool) -> Result<(), CdslError> {
        let c_name = CString::new(name)?;
        unsafe { ffi::cdsl_context_set_bool(self.raw, c_name.as_ptr(), if val { 1 } else { 0 }) };
        Ok(())
    }

    pub fn set_string(&mut self, name: &str, val: &str) -> Result<(), CdslError> {
        let c_name = CString::new(name)?;
        let c_val = CString::new(val)?;
        unsafe { ffi::cdsl_context_set_string(self.raw, c_name.as_ptr(), c_val.as_ptr()) };
        Ok(())
    }

    pub fn get_int(&self, name: &str, default_val: i32) -> Result<i32, CdslError> {
        let c_name = CString::new(name)?;
        let val = unsafe { ffi::cdsl_context_get_int(self.raw, c_name.as_ptr(), default_val) };
        Ok(val)
    }

    pub fn get_float(&self, name: &str, default_val: f64) -> Result<f64, CdslError> {
        let c_name = CString::new(name)?;
        let val = unsafe { ffi::cdsl_context_get_float(self.raw, c_name.as_ptr(), default_val) };
        Ok(val)
    }

    pub fn get_bool(&self, name: &str, default_val: bool) -> Result<bool, CdslError> {
        let c_name = CString::new(name)?;
        let val = unsafe {
            ffi::cdsl_context_get_bool(self.raw, c_name.as_ptr(), if default_val { 1 } else { 0 })
        };
        Ok(val != 0)
    }

    pub fn get_string(&self, name: &str, default_val: &str) -> Result<String, CdslError> {
        let c_name = CString::new(name)?;
        let c_def = CString::new(default_val)?;
        let ptr = unsafe { ffi::cdsl_context_get_string(self.raw, c_name.as_ptr(), c_def.as_ptr()) };
        if ptr.is_null() {
            return Ok(default_val.to_string());
        }
        Ok(unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() })
    }
}

impl Drop for Context {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            unsafe { ffi::cdsl_context_free(self.raw) };
            self.raw = std::ptr::null_mut();
        }
    }
}

/// Parsed CDSL rule.
pub struct Rule {
    raw: *mut ffi::CdslRule,
}

unsafe impl Send for Rule {}
unsafe impl Sync for Rule {}

impl Rule {
    /// Parses a rule from DSL source string.
    pub fn parse(dsl: &str) -> Result<Self, CdslError> {
        let c_dsl = CString::new(dsl)?;
        let raw = unsafe { ffi::cdsl_parse_string(c_dsl.as_ptr(), std::ptr::null_mut()) };
        if raw.is_null() {
            return Err(CdslError::ParseError);
        }
        Ok(Self { raw })
    }
}

impl Drop for Rule {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            unsafe { ffi::cdsl_free_rule(self.raw) };
            self.raw = std::ptr::null_mut();
        }
    }
}

/// Decision Table evaluation report.
#[derive(Debug, Clone)]
pub struct DtableReport {
    pub table_name: String,
    pub matched: bool,
    pub num_matches: usize,
}

/// Parsed Decision Table.
pub struct DecisionTable {
    raw: *mut ffi::CdslDtable,
}

unsafe impl Send for DecisionTable {}
unsafe impl Sync for DecisionTable {}

impl DecisionTable {
    /// Parses a decision table from DSL string.
    pub fn parse(dsl: &str) -> Result<Self, CdslError> {
        let c_dsl = CString::new(dsl)?;
        let raw = unsafe { ffi::cdsl_dtable_parse_string(c_dsl.as_ptr(), std::ptr::null_mut()) };
        if raw.is_null() {
            return Err(CdslError::ParseError);
        }
        Ok(Self { raw })
    }

    /// Evaluates the decision table against a context.
    pub fn evaluate(&self, ctx: &mut Context) -> Result<DtableReport, CdslError> {
        let raw_report = unsafe { ffi::cdsl_dtable_eval(self.raw, ctx.raw) };
        if raw_report.is_null() {
            return Err(CdslError::EvaluationFailed);
        }
        let rpt = unsafe { &*raw_report };
        let table_name = if !rpt.table_name.is_null() {
            unsafe { CStr::from_ptr(rpt.table_name).to_string_lossy().into_owned() }
        } else {
            String::new()
        };
        let report = DtableReport {
            table_name,
            matched: rpt.matched,
            num_matches: rpt.num_matches,
        };
        unsafe { ffi::cdsl_dtable_report_free(raw_report) };
        Ok(report)
    }
}

impl Drop for DecisionTable {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            unsafe { ffi::cdsl_dtable_free(self.raw) };
            self.raw = std::ptr::null_mut();
        }
    }
}

/// Scorecard evaluation report.
#[derive(Debug, Clone)]
pub struct ScorecardReport {
    pub scorecard_name: String,
    pub baseline_score: f64,
    pub final_score: f64,
}

/// Parsed Scorecard model.
pub struct Scorecard {
    raw: *mut ffi::CdslScorecard,
}

unsafe impl Send for Scorecard {}
unsafe impl Sync for Scorecard {}

impl Scorecard {
    /// Parses a scorecard model from DSL string.
    pub fn parse(dsl: &str) -> Result<Self, CdslError> {
        let c_dsl = CString::new(dsl)?;
        let raw = unsafe { ffi::cdsl_scorecard_parse_string(c_dsl.as_ptr(), std::ptr::null_mut()) };
        if raw.is_null() {
            return Err(CdslError::ParseError);
        }
        Ok(Self { raw })
    }

    /// Evaluates the scorecard against a context.
    pub fn evaluate(&self, ctx: &mut Context) -> Result<ScorecardReport, CdslError> {
        let raw_report = unsafe { ffi::cdsl_scorecard_eval(self.raw, ctx.raw) };
        if raw_report.is_null() {
            return Err(CdslError::EvaluationFailed);
        }
        let rpt = unsafe { &*raw_report };
        let scorecard_name = if !rpt.scorecard_name.is_null() {
            unsafe { CStr::from_ptr(rpt.scorecard_name).to_string_lossy().into_owned() }
        } else {
            String::new()
        };
        let report = ScorecardReport {
            scorecard_name,
            baseline_score: rpt.baseline_score,
            final_score: rpt.final_score,
        };
        unsafe { ffi::cdsl_scorecard_report_free(raw_report) };
        Ok(report)
    }
}

impl Drop for Scorecard {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            unsafe { ffi::cdsl_scorecard_free(self.raw) };
            self.raw = std::ptr::null_mut();
        }
    }
}

/// Pipeline execution report.
#[derive(Debug, Clone)]
pub struct PipelineReport {
    pub pipeline_name: String,
    pub status: i32,
    pub num_stages: usize,
    pub total_duration_us: f64,
}

/// Parsed DAG pipeline.
pub struct Pipeline {
    raw: *mut ffi::CdslPipeline,
}

unsafe impl Send for Pipeline {}
unsafe impl Sync for Pipeline {}

impl Pipeline {
    /// Parses a pipeline from DSL string.
    pub fn parse(dsl: &str) -> Result<Self, CdslError> {
        let c_dsl = CString::new(dsl)?;
        let raw = unsafe { ffi::cdsl_pipeline_parse_string(c_dsl.as_ptr(), std::ptr::null_mut()) };
        if raw.is_null() {
            return Err(CdslError::ParseError);
        }
        Ok(Self { raw })
    }

    /// Executes the pipeline stages.
    pub fn execute(&self, engine: &Engine, ctx: &mut Context) -> Result<PipelineReport, CdslError> {
        let raw_report = unsafe { ffi::cdsl_pipeline_execute(self.raw, engine.raw, ctx.raw) };
        if raw_report.is_null() {
            return Err(CdslError::EvaluationFailed);
        }
        let rpt = unsafe { &*raw_report };
        let pipeline_name = if !rpt.pipeline_name.is_null() {
            unsafe { CStr::from_ptr(rpt.pipeline_name).to_string_lossy().into_owned() }
        } else {
            String::new()
        };
        let report = PipelineReport {
            pipeline_name,
            status: rpt.status,
            num_stages: rpt.num_stages,
            total_duration_us: rpt.total_duration_us,
        };
        unsafe { ffi::cdsl_pipeline_report_free(raw_report) };
        Ok(report)
    }
}

impl Drop for Pipeline {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            unsafe { ffi::cdsl_pipeline_free(self.raw) };
            self.raw = std::ptr::null_mut();
        }
    }
}
