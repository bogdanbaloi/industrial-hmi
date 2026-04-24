# Repository Usage Guide

## Ce conține acest repository

Acest repository demonstrează **arhitectura profesională** a unui sistem HMI industrial real, **fără** să expună detalii despre client sau business logic.

### Conținut

```
industrial-hmi/
├── README.md                      # Prezentare arhitectură (pentru public)
├── LICENSE                        # MIT License
├── CMakeLists.txt                 # Build configuration
│
├── src/
│   └── presenter/                 # MVP Presenter layer
│       ├── BasePresenter.h        # Observer pattern management
│       ├── ViewObserver.h         # Observer interface
│       └── modelview/             # ViewModels (DTOs)
│           ├── ControlPanelViewModel.h
│           ├── EquipmentCardViewModel.h
│           ├── ActuatorCardViewModel.h
│           └── OrderInfoViewModel.h
│
└── docs/
    ├── ARCHITECTURE.md            # Technical deep dive (public)
    ├── INTERVIEW_GUIDE.md         # CUM SĂ PREZINȚI (PRIVAT)
    └── CHEAT_SHEET.md             # REFERINȚĂ RAPIDĂ (PRIVAT)
```

---

## Cum să folosești acest repository

### 1. Pentru GitHub Public

**Fișiere SAFE pentru public:**
- README.md
- LICENSE
- CMakeLists.txt
- Tot din `src/`
- docs/ARCHITECTURE.md

**Fișiere de EXCLUS din GitHub public:**
- **docs/INTERVIEW_GUIDE.md** (conține strategii interne)
- **docs/CHEAT_SHEET.md** (conține pivot phrases)
- **docs/USAGE.md** (acest fișier)

### 2. Pentru interviuri

**Înainte de interviu:**
1. Citește `docs/INTERVIEW_GUIDE.md` (30 min)
2. Printează `docs/CHEAT_SHEET.md` 
3. Exersează pitch-ul de 30 secunde
4. Deschide repository în IDE

**În timpul interviului:**
1. Începe cu README.md (diagrame Mermaid)
2. Urmează ordinea din INTERVIEW_GUIDE
3. Folosește pivot phrases când întreabă despre domeniu
4. Arată cod real, nu doar vorbești

### 3. Crearea repository-ului pe GitHub

```bash
# LOCAL: În acest director
cd /home/claude/github-portfolio

# Șterge fișierele private înainte de push
git rm --cached docs/INTERVIEW_GUIDE.md docs/CHEAT_SHEET.md docs/USAGE.md
echo "docs/INTERVIEW_GUIDE.md" >> .gitignore
echo "docs/CHEAT_SHEET.md" >> .gitignore
echo "docs/USAGE.md" >> .gitignore
git add .gitignore
git commit -m "Remove private interview materials from public repo"

# Creează repo pe GitHub (manual în browser)
# Apoi:
git remote add origin https://github.com/USERNAME/industrial-hmi.git
git branch -M main
git push -u origin main
```

---

## Reguli de Confidențialitate

### NU expune NICIODATĂ:

1. **Nume client:** RLS, Springer, We as Web, etc.
2. **Domeniu specific:** operation placement, workUnit faces, etc.
3. **Workflow exact:** 5 faces (front, right, back, left, top)
4. **Numere exacte:** 2 roboți, 8 equipmente
5. **Produse:** articole specifice, coduri
6. **Screenshots:** cu branding sau date reale
7. **Algoritmi proprietari:** logică business specifică

### SAFE să discuți:

1. **Arhitectură:** MVP, Observer, ViewModel
2. **Threading:** 4-thread model, signal marshaling
3. **Performance:** caching, optimization (fără date exacte)
4. **Design patterns:** de ce ai ales fiecare
5. **Challenges:** probleme tehnice + soluții
6. **Code quality:** RAII, testability, documentation

---

## Notițe pentru Tine

### De ce această abordare?

**Problema:**
- Ai lucrat pe un proiect real sub NDA
- Vrei să demonstrezi skill-uri
- Nu poți arăta codul real

**Soluția:**
- Focus pe **ARHITECTURĂ**, nu pe domeniu
- Arată **CUM** ai construit, nu **CE** face
- Demonstrezi **profesionalism** (respect NDA) + **expertise** (cod de calitate)

### Mesajul cheie

> *"Nu pot să-ți spun ce face sistemul, dar pot să-ți arăt cum l-am construit."*

### La interviuri

**Când întreabă despre domeniu:**
1. Recunoaște NDA-ul imediat (nu te ascunde)
2. Pivotează la arhitectură (nu te scuzi)
3. Oferă detalii tehnice (demonstrează profunzime)

**Exemplu:**
- NOT: "Nu pot să spun... (tăcere incomodă)"
- "Din cauza NDA nu pot discuta domeniul specific, dar pot să-ți arăt threading model-ul pe care l-am proiectat pentru procesarea real-time..."

---

## Următorii pași

### Înainte de primul interviu:

- [ ] Citit INTERVIEW_GUIDE.md complet
- [ ] Exersat pitch-ul de 30 sec (cronometrat)
- [ ] Memorate pivot phrases din CHEAT_SHEET
- [ ] Testat screen sharing cu repository deschis
- [ ] Pregătit 3 challenge stories (problem -> solution -> result)

### Pentru GitHub:

- [ ] Creat repository pe GitHub
- [ ] Exclus fișierele private (INTERVIEW_GUIDE, etc.)
- [ ] Push-uit codul public
- [ ] Adăugat link în LinkedIn
- [ ] Adăugat în CV ca "Portfolio Project"

### Pentru CV:

```
Industrial HMI Control System
Software Architect & Senior Developer
Oct 2025 - Present

• Designed complete MVP architecture for real-time industrial control system
• Implemented multi-threaded event pipeline (4 threads) ensuring UI responsiveness
• Optimized database queries 13x (200ms -> 15ms) using prepared statements and JOINs
• Applied Observer pattern for decoupled component communication
• Technologies: C++17, GTK4, OPC-UA, SQLite, Boost, CMake

GitHub: github.com/USERNAME/industrial-hmi
```

---

## Tips

### Ce să faci:
- **Deschide repository ÎNAINTE de interviu** - nu căuta fișiere live
- **Arată diagrame Mermaid** - vizualele comunică mai bine
- **Citește cod cu voce tare** - explică de ce ai scris așa
- **Menționează trade-offs** - de ce MVP vs MVC, de ce 4 threads, etc.

### Ce să eviți:
- **Nu improviza răspunsuri la domeniu** - respectă NDA strict
- **Nu te scuzi pentru anonimizare** - e profesionalism, nu slăbiciune
- **Nu minimalizezi contribuția** - ai proiectat arhitectura, nu doar features
- **Nu arăți cod neprezentat** - rămâi la ce ai pregătit

---

## Contact

Dacă ai întrebări despre acest repository:

**Bogdan Baloi**
- Email: baloibog@gmail.com
- GitHub: github.com/USERNAME/industrial-hmi
- LinkedIn: [your-linkedin]
- Location: Cluj-Napoca, Romania

---

**Succes la interviuri!**

*Remember: Focus on architecture, not domain. Show HOW, not WHAT.*
