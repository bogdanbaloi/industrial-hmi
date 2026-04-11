# Quick Reference - Interview Cheat Sheet

> 1-page summary for quick prep before interviews

## 30-Second Pitch

*"I architected an industrial HMI system with MVP pattern and multi-threaded design. Due to NDA I can't share business specifics, but I can show you the architectural patterns, threading model, and technical challenges I solved. The code demonstrates production-grade C++17, design patterns, and real-time performance optimization."*

---

## Safe Topics (Talk Freely)

✅ **Architecture:** MVP pattern, Observer pattern, ViewModel/DTO
✅ **Threading:** 4-thread model, Glib::signal_idle, Boost::Signals2, mutex vs atomic
✅ **Performance:** ViewModel caching (10/sec → 2/sec), DB optimization (200ms → 15ms)
✅ **Testability:** Unit testing Presenter layer, mock observers
✅ **Code Quality:** RAII, smart pointers, const-correctness
✅ **Technologies:** C++17, GTK4, OPC-UA, SQLite, CMake, Boost

---

## Redirect Topics (Pivot to Architecture)

⚠️ **Business Domain** → "Can't share due to NDA, but the architecture applies to any manufacturing execution system..."

⚠️ **Specific Workflow** → "I can't discuss workflow specifics, but let me show you how the multi-threaded event pipeline handles real-time updates..."

⚠️ **Equipment Details** → "Due to confidentiality I can't specify, but the Observer pattern makes it scalable - adding new equipment types is trivial..."

⚠️ **Production Numbers** → "Can't share metrics, but I can show you the performance optimizations I implemented..."

---

## Decline Topics (Don't Answer)

❌ Client name / Industry / Location
❌ Specific products manufactured  
❌ Exact equipment counts
❌ Business logic details
❌ Screenshots with branding

**Response:** *"I'm under NDA and can't share that. However, I can walk you through [architecture/threading/patterns/code quality]..."*

---

## Code Walkthrough Order

1. **Architecture diagrams** (README.md) - 2 min
2. **ViewModels** (modelview/*.h) - 3 min
3. **Observer pattern** (ViewObserver.h, BasePresenter.h) - 5 min
4. **Threading model** (ARCHITECTURE.md) - 5 min
5. **Performance examples** (caching, prepared statements) - 3 min

---

## Key Technical Points

**Why MVP?**
- Testability: Business logic independent of UI
- Flexibility: Can swap UI frameworks
- Separation: Each layer has single responsibility

**Why 4 threads?**
- Hardware thread: OPC-UA I/O (blocks 100ms+)
- App thread: State machines, DB writes (serialized)
- Presenter thread: Data transformation (heavy computation)
- UI thread: GTK rendering only (never blocks)

**Why Observer pattern?**
- Decoupling: Presenter doesn't know about specific Views
- Extensibility: Multiple Views can observe one Presenter
- Testability: Mock observers in unit tests

**Key Optimizations:**
- ViewModel caching: 5x fewer UI updates
- Prepared statements: 5x faster queries
- JOIN instead of multiple SELECTs: 13x faster
- Atomic flags for simple state: Lock-free

---

## Common Questions - Quick Answers

**Q: Why anonymized?**
A: NDA restrictions. Shows professionalism + technical depth without exposing client IP.

**Q: How do I know it's real?**
A: [Show code depth] - production-grade patterns, threading, optimization, documentation.

**Q: What was hardest bug?**
A: Race condition in control panel updates. [Show threading solution with mutex + atomics]

**Q: How did you test?**
A: Unit tests (Presenter layer with mocks), integration tests (OPC-UA simulator), ThreadSanitizer, Valgrind.

**Q: Why C++?**
A: Real-time requirements, memory control (RAII), platform support, legacy integration.

**Q: MVP vs MVC?**
A: MVP: View is dumb, all logic in Presenter. MVC: View often has some logic. [Show ViewModel example]

---

## Pivot Phrases (Memorize These)

1. "Due to NDA I can't discuss that, but I can show you..."
2. "I'm not able to share business specifics, however the architecture..."
3. "Client confidentiality prevents me from sharing that detail, but let me walk you through..."
4. "I can't talk about the domain, but I can explain the technical challenges..."

---

## What They're Really Evaluating

✅ **Technical depth** - Do you understand what you built?
✅ **Communication** - Can you explain complex topics clearly?
✅ **Professionalism** - Do you respect NDAs?
✅ **Problem-solving** - How did you handle challenges?
✅ **Code quality** - Is this production-grade work?

❌ **NOT:** Specific business domain knowledge

---

## Pre-Interview Checklist

- [ ] Practiced 2-minute architecture explanation
- [ ] Can navigate to key files quickly
- [ ] Reviewed threading model diagram
- [ ] Prepared 3 technical challenges + solutions
- [ ] Comfortable saying "I can't share that"
- [ ] Laptop charged, screen sharing tested
- [ ] Repository open in IDE, README visible

---

**Remember:** Focus on HOW you built it, not WHAT it does.

**You're demonstrating:** Architecture skills, code quality, problem-solving ability, professional ethics.

---

Print this before your interview! 📄
