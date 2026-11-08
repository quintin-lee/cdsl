package cdsl

/*
#cgo CFLAGS: -I../../include -I../../build/include
#cgo LDFLAGS: -L../../build -lcdsl
#include <stdlib.h>
#include "cdsl/cdsl.h"
*/
import "C"

import (
	"errors"
	"runtime"
	"unsafe"
)

// Status represents the evaluation outcome.
type Status int

const (
	StatusPassed          Status = 0
	StatusPartiallyPassed Status = 1
	StatusFailed          Status = 2
	StatusError           Status = 3
)

// Engine wraps the CDSL virtual machine.
type Engine struct {
	ptr *C.cdsl_vm_t
}

// NewEngine initializes a new CDSL virtual machine instance.
func NewEngine() (*Engine, error) {
	ptr := C.cdsl_vm_create(nil)
	if ptr == nil {
		return nil, errors.New("failed to allocate CDSL VM")
	}
	e := &Engine{ptr: ptr}
	runtime.SetFinalizer(e, (*Engine).Close)
	return e, nil
}

// Close releases the VM resources.
func (e *Engine) Close() {
	if e.ptr != nil {
		C.cdsl_vm_free(e.ptr)
		e.ptr = nil
	}
}

// Context stores runtime input variables for rule evaluations.
type Context struct {
	ptr *C.cdsl_context_t
}

// NewContext creates a new variable evaluation context.
func NewContext() (*Context, error) {
	ptr := C.cdsl_context_create(nil)
	if ptr == nil {
		return nil, errors.New("failed to create CDSL context")
	}
	ctx := &Context{ptr: ptr}
	runtime.SetFinalizer(ctx, (*Context).Close)
	return ctx, nil
}

// Close releases context memory.
func (c *Context) Close() {
	if c.ptr != nil {
		C.cdsl_context_free(c.ptr)
		c.ptr = nil
	}
}

// SetInt sets an integer variable in the context.
func (c *Context) SetInt(name string, val int64) {
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))
	C.cdsl_context_set_int(c.ptr, cName, C.int(val))
}

// SetFloat sets a float variable in the context.
func (c *Context) SetFloat(name string, val float64) {
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))
	C.cdsl_context_set_float(c.ptr, cName, C.double(val))
}

// SetBool sets a boolean variable in the context.
func (c *Context) SetBool(name string, val bool) {
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))
	cVal := C.int(0)
	if val {
		cVal = 1
	}
	C.cdsl_context_set_bool(c.ptr, cName, cVal)
}

// SetString sets a string variable in the context.
func (c *Context) SetString(name string, val string) {
	cName := C.CString(name)
	cVal := C.CString(val)
	defer C.free(unsafe.Pointer(cName))
	defer C.free(unsafe.Pointer(cVal))
	C.cdsl_context_set_string(c.ptr, cName, cVal)
}

// GetInt retrieves an integer variable from context.
func (c *Context) GetInt(name string, defaultVal int64) int64 {
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))
	return int64(C.cdsl_context_get_int(c.ptr, cName, C.int(defaultVal)))
}

// GetFloat retrieves a float variable from context.
func (c *Context) GetFloat(name string, defaultVal float64) float64 {
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))
	return float64(C.cdsl_context_get_float(c.ptr, cName, C.double(defaultVal)))
}

// GetBool retrieves a boolean variable from context.
func (c *Context) GetBool(name string, defaultVal bool) bool {
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))
	cDef := C.int(0)
	if defaultVal {
		cDef = 1
	}
	return C.cdsl_context_get_bool(c.ptr, cName, cDef) != 0
}

// GetString retrieves a string variable from context.
func (c *Context) GetString(name string, defaultVal string) string {
	cName := C.CString(name)
	cDef := C.CString(defaultVal)
	defer C.free(unsafe.Pointer(cName))
	defer C.free(unsafe.Pointer(cDef))
	res := C.cdsl_context_get_string(c.ptr, cName, cDef)
	if res == nil {
		return defaultVal
	}
	return C.GoString(res)
}

// Rule represents a parsed CDSL rule.
type Rule struct {
	ptr *C.cdsl_rule_t
}

// ParseRule parses a rule from DSL source code.
func ParseRule(dsl string) (*Rule, error) {
	cDsl := C.CString(dsl)
	defer C.free(unsafe.Pointer(cDsl))
	ptr := C.cdsl_parse_string(cDsl, nil)
	if ptr == nil {
		return nil, errors.New("failed to parse CDSL rule")
	}
	r := &Rule{ptr: ptr}
	runtime.SetFinalizer(r, (*Rule).Close)
	return r, nil
}

// Close frees rule memory.
func (r *Rule) Close() {
	if r.ptr != nil {
		C.cdsl_free_rule(r.ptr)
		r.ptr = nil
	}
}

// Report contains rule execution results.
type Report struct {
	RuleName    string
	Status      Status
	Score       int
	MaxScore    int
	Summary     string
	Passed      bool
}

// Execute evaluates a rule against the context.
func (e *Engine) Execute(r *Rule, ctx *Context) (*Report, error) {
	if e.ptr == nil || r.ptr == nil || ctx.ptr == nil {
		return nil, errors.New("invalid nil engine, rule, or context")
	}
	cReport := C.cdsl_vm_execute(e.ptr, r.ptr, ctx.ptr)
	if cReport == nil {
		return nil, errors.New("rule execution returned null report")
	}
	defer C.cdsl_report_free(cReport)

	rpt := &Report{
		Status:   Status(cReport.status),
		Score:    int(cReport.total_obtained_score),
		MaxScore: int(cReport.total_max_score),
		Passed:   cReport.status == C.CDSL_STATUS_PASSED,
	}
	if cReport.rule_name != nil {
		rpt.RuleName = C.GoString(cReport.rule_name)
	}
	if cReport.decision_summary != nil {
		rpt.Summary = C.GoString(cReport.decision_summary)
	}
	return rpt, nil
}

// DecisionTable represents a parsed DMN-style decision table.
type DecisionTable struct {
	ptr *C.cdsl_dtable_t
}

// ParseDecisionTable parses a decision table from DSL string.
func ParseDecisionTable(dsl string) (*DecisionTable, error) {
	cDsl := C.CString(dsl)
	defer C.free(unsafe.Pointer(cDsl))
	ptr := C.cdsl_dtable_parse_string(cDsl, nil)
	if ptr == nil {
		return nil, errors.New("failed to parse decision table")
	}
	dt := &DecisionTable{ptr: ptr}
	runtime.SetFinalizer(dt, (*DecisionTable).Close)
	return dt, nil
}

// Close frees decision table memory.
func (dt *DecisionTable) Close() {
	if dt.ptr != nil {
		C.cdsl_dtable_free(dt.ptr)
		dt.ptr = nil
	}
}

// DecisionReport holds decision table evaluation results.
type DecisionReport struct {
	TableName string
	Matched   bool
	NumMatches int
}

// Evaluate evaluates the decision table.
func (dt *DecisionTable) Evaluate(ctx *Context) (*DecisionReport, error) {
	if dt.ptr == nil || ctx.ptr == nil {
		return nil, errors.New("invalid nil table or context")
	}
	cRpt := C.cdsl_dtable_eval(dt.ptr, ctx.ptr)
	if cRpt == nil {
		return nil, errors.New("decision table evaluation failed")
	}
	defer C.cdsl_dtable_report_free(cRpt)

	rpt := &DecisionReport{
		Matched:    bool(cRpt.matched),
		NumMatches: int(cRpt.num_matches),
	}
	if cRpt.table_name != nil {
		rpt.TableName = C.GoString(cRpt.table_name)
	}
	return rpt, nil
}

// Scorecard represents a credit/risk scorecard model.
type Scorecard struct {
	ptr *C.cdsl_scorecard_t
}

// ParseScorecard parses a scorecard from DSL string.
func ParseScorecard(dsl string) (*Scorecard, error) {
	cDsl := C.CString(dsl)
	defer C.free(unsafe.Pointer(cDsl))
	ptr := C.cdsl_scorecard_parse_string(cDsl, nil)
	if ptr == nil {
		return nil, errors.New("failed to parse scorecard")
	}
	sc := &Scorecard{ptr: ptr}
	runtime.SetFinalizer(sc, (*Scorecard).Close)
	return sc, nil
}

// Close frees scorecard memory.
func (sc *Scorecard) Close() {
	if sc.ptr != nil {
		C.cdsl_scorecard_free(sc.ptr)
		sc.ptr = nil
	}
}

// ScorecardReport holds scorecard evaluation results.
type ScorecardReport struct {
	ScorecardName string
	BaselineScore float64
	FinalScore    float64
}

// Evaluate evaluates the scorecard.
func (sc *Scorecard) Evaluate(ctx *Context) (*ScorecardReport, error) {
	if sc.ptr == nil || ctx.ptr == nil {
		return nil, errors.New("invalid nil scorecard or context")
	}
	cRpt := C.cdsl_scorecard_eval(sc.ptr, ctx.ptr)
	if cRpt == nil {
		return nil, errors.New("scorecard evaluation failed")
	}
	defer C.cdsl_scorecard_report_free(cRpt)

	rpt := &ScorecardReport{
		BaselineScore: float64(cRpt.baseline_score),
		FinalScore:    float64(cRpt.final_score),
	}
	if cRpt.scorecard_name != nil {
		rpt.ScorecardName = C.GoString(cRpt.scorecard_name)
	}
	return rpt, nil
}

// Pipeline represents a multi-stage DAG rule execution pipeline.
type Pipeline struct {
	ptr *C.cdsl_pipeline_t
}

// ParsePipeline parses a pipeline from DSL string.
func ParsePipeline(dsl string) (*Pipeline, error) {
	cDsl := C.CString(dsl)
	defer C.free(unsafe.Pointer(cDsl))
	ptr := C.cdsl_pipeline_parse_string(cDsl, nil)
	if ptr == nil {
		return nil, errors.New("failed to parse pipeline")
	}
	p := &Pipeline{ptr: ptr}
	runtime.SetFinalizer(p, (*Pipeline).Close)
	return p, nil
}

// Close frees pipeline memory.
func (p *Pipeline) Close() {
	if p.ptr != nil {
		C.cdsl_pipeline_free(p.ptr)
		p.ptr = nil
	}
}

// PipelineReport holds pipeline execution results.
type PipelineReport struct {
	PipelineName string
	Status       int
	NumStages    int
	DurationUs   float64
}

// Execute executes the pipeline stages.
func (p *Pipeline) Execute(e *Engine, ctx *Context) (*PipelineReport, error) {
	if p.ptr == nil || e.ptr == nil || ctx.ptr == nil {
		return nil, errors.New("invalid nil pipeline, engine, or context")
	}
	cRpt := C.cdsl_pipeline_execute(p.ptr, e.ptr, ctx.ptr)
	if cRpt == nil {
		return nil, errors.New("pipeline execution failed")
	}
	defer C.cdsl_pipeline_report_free(cRpt)

	rpt := &PipelineReport{
		Status:     int(cRpt.status),
		NumStages:  int(cRpt.num_stages),
		DurationUs: float64(cRpt.total_duration_us),
	}
	if cRpt.pipeline_name != nil {
		rpt.PipelineName = C.GoString(cRpt.pipeline_name)
	}
	return rpt, nil
}
