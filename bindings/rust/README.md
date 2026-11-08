# CDSL Rust Crate

High-performance, memory-safe Rust bindings for **CDSL (C Domain Specific Language)**.

## Features
- **Idiomatic Rust**: Complete RAII safety (`Drop`), thread safety (`Send + Sync`), and typed results with `Result<T, CdslError>`.
- **Full Engine Support**: Evaluate Rules, DMN Decision Tables, Credit/Risk Scorecards, and Multi-stage DAG Pipelines.
- **Zero Memory Leaks**: Direct integration with CDSL's native C memory management and bump-allocator arenas.

## Usage

```rust
use cdsl::{Engine, Context, Rule};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let engine = Engine::new()?;
    let mut ctx = Context::new()?;
    ctx.set_int("credit_score", 720)?;

    let rule = Rule::parse("RULE check { WHEN credit_score >= 700 THEN approve() }")?;
    let report = engine.execute(&rule, &mut ctx)?;

    println!("Rule: {}, Status: {:?}", report.rule_name, report.status);
    Ok(())
}
```
