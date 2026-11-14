# LLM Coding Assistant Benchmark

A side-by-side comparison of AI coding assistants through the implementation of a C++ limit order book and matching engine.

## Overview

This project explores how different AI coding assistants approach the same software engineering problem. Each assistant is tasked with independently building a limit order book and matching engine in C++, along with supporting components for generating synthetic market data, replaying events, testing correctness, and measuring performance.

The goal is to compare the resulting implementations in terms of:

- **Code quality:** Organization, readability, maintainability, and design decisions.
- **Correctness:** Whether the matching engine handles different order types and market scenarios correctly.
- **Architecture:** Data structures, algorithms, and overall implementation choices.
- **Performance:** Throughput, average latency, and behavior across different workloads.
- **Development experience:** How effectively each coding assistant understands requirements and produces a working implementation.

## Project Structure

```text
llm-benchmarking/
├── claude-code-orderbook/
│   └── ...
├── copilot-orderbook/
│   └── ...
└── README.md
```

### Implementations

| Project | AI Coding Assistant | Description |
|---|---|---|
| `claude-code-orderbook` | Claude Code | A more detailed C++20 order book implementation with synthetic market-feed generation, replay, profiling, and performance analysis. |
| `copilot-orderbook` | GitHub Copilot | An independently developed C++20 order book implementation with a simpler design, synthetic workloads, correctness tests, and benchmarking. |

Each implementation is maintained in its own directory to keep the projects independent.

## Order Book Requirements

Both implementations are based on a single-instrument limit order book and matching engine.

Core functionality includes:

- Limit and market buy/sell orders
- Price-time priority
- Partial and complete order fills
- Matching across multiple price levels
- Order cancellation by ID
- Best bid and ask tracking
- Market depth inspection

Prices are represented using integer ticks to avoid floating-point precision issues.

## Synthetic Market Data

Each implementation includes a synthetic market-feed generator to produce deterministic workloads for testing and benchmarking.

The generated events simulate common order book activity, including order submissions and cancellations. Different workload distributions help evaluate how the implementations behave under varying market conditions.

No external historical market data is required.

## Benchmarking

The implementations include benchmarks designed to evaluate performance at different event volumes.

Metrics of interest include:

- **Throughput:** Number of events processed per second.
- **Average latency:** Average time required to process an event.
- **Workload behavior:** Performance under different distributions of order submissions, cancellations, and aggressive orders.

Benchmark results should be interpreted alongside the workload configuration, compiler settings, and execution environment.

For a fair comparison, both implementations should eventually be tested against the same event sequences and under the same build and runtime conditions.

## Goals

The broader goal of this experiment is to better understand the strengths and tradeoffs of AI-assisted software development, particularly when working on performance-sensitive backend systems.

Rather than evaluating coding assistants based solely on whether they can produce a working application, this project examines the engineering decisions behind their implementations and how those decisions affect correctness, maintainability, and performance.

## Technologies

- C++20
- CMake
- Git
- Claude Code
- GitHub Copilot

