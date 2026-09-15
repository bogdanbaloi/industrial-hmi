# Build log - componenta Rust SPSC

Jurnalul deciziilor și al drumului, ca o poveste. Complementar cu ADR-ul
(decizia formală) și REQUIREMENTS (contractul). Aici stau raționamentul,
alternativele și termenii noi explicați pe măsură ce apar. Curatat, nu
transcript brut. Pe truth rail: doar ce s-a întâmplat cu adevărat.

Termenii tehnici standard rămân în engleză (thread, mutex, queue, buffer,
producer/consumer etc.); proza e în română. Un glosar cu toți termenii e la
finalul fișierului.

---

## 2026-09-15 - De ce Rust și ce construim

Piața cere Rust tot mai des lângă C++ (Empiric, SES, K-tronik doar în
ultimele zile). În loc să învățăm Rust „în gol", luăm cea mai bine testată
piesă de concurență din portofoliu - queue-ul **SPSC lock-free** din C++
(`src/core/SpscQueue.h`) - și o rescriem în Rust.

- **SPSC** = single-producer / single-consumer: exact un thread scrie, exact
  un thread citește. Sub regula asta, queue-ul nu are nevoie de mutex.
- **lock-free** = fără mutex; thread-urile se coordonează prin operații
  atomice, nu prin blocare.

De ce e o poveste bună de interviu: **nu învățăm algoritmul, doar limbajul**.
Deții deja algoritmul din C++. Iar comparația e de aur - aceeași structură,
două modele de siguranță:
- în C++ contractul „un producer, un consumer" e ținut prin comentariu
  plus ThreadSanitizer (dinamic, prinde greșeala la rulare);
- în Rust îl mută **type-system-ul**: la construcție primești două obiecte
  distincte, un `Producer` și un `Consumer`, și nu ai cum să faci doi.

## 2026-09-15 - Decizia de arhitectură: componentă în portofoliu, nu orfan

Întrebarea lui Bogdan: „cum o legăm de portofoliu, ca Qt și MCP?"

Qt și MCP au fost extensii ale ACELUIAȘI repo: un core C++ bine făcut, mai
mulți consumers. Rust e alt limbaj, deci nu poate consuma direct core-ul
C++. Soluția aleasă (vezi ADR-0026):
- crate-ul Rust stă ÎN repo, în folderul `rust/spsc/` la rădăcină (nu în
  `src/`, care e scanat de CMake pentru C++ și ar forța un „src în src");
- **Faza 1:** crate standalone care se compilează și se testează singur;
- **Faza 2:** îl legăm de HMI-ul C++ peste un **C ABI** (aceeași tehnică de
  la plugin-ul ONNX), ca HMI-ul să apeleze codul Rust.

Termeni:
- **crate** = unitatea de cod în Rust, adică un proiect/bibliotecă
  împachetată. Echivalentul unei librării cu CMake din lumea C++.
- **C ABI** (Application Binary Interface) = o „priză" standard prin care
  cod scris în două limbaje se poate apela reciproc la nivel binar.

Alternative respinse: repo separat (crate orfan, poveste mai slabă) și în
`src/` (src în src + riscă să încurce CMake).

## 2026-09-15 - Toolchain: saga MSVC vs GNU

Ca să compilezi Rust îți trebuie un **toolchain** (setul de unelte:
compiler + linker + librării standard). L-am instalat cu **rustup**
(managerul oficial de toolchain-uri Rust), prin winget.

Problema: instalatorul a pus default toolchain-ul **MSVC**, care are nevoie
de linker-ul de la Visual Studio (`link.exe`). Am verificat - nu ai Visual
Studio (nici cl.exe, nici link.exe). Deci MSVC n-ar fi legat nimic.

Soluția: toolchain-ul **GNU** (`x86_64-pc-windows-gnu`), care își aduce
propriul linker (MinGW), nu depinde de Visual Studio și se potrivește cu
mediul tău MSYS2/CLANG64 (și cu interop-ul de la Faza 2). L-am instalat și
am verificat că **chiar compilează și leagă**.

Notă (regulă nouă a lui Bogdan, 2026-09-15): înainte de orice instalare,
întreb și aștept da/nu.

## 2026-09-15 - Scaffold-ul (Faza 1, pasul 1)

**Scaffold** = schela. Ca la o clădire: întâi ridici schela și structura,
apoi torni pereții. La cod, scaffold-ul e scheletul gol al proiectului -
foldere + fișiere de configurare + fișiere aproape goale - înainte de logica
grea.

Am creat, pe branch-ul `feature/rust-spsc` (de pe main proaspăt):
- `Cargo.toml` - manifestul proiectului (rețeta: nume, ediție, dependențe),
  în format **TOML** (un format de configurare simplu, cheie = valoare cu
  `[secțiuni]`; ca JSON/YAML, dar mai curat pentru scris de mână).
  Echivalentul lui `CMakeLists.txt`.
- `src/lib.rs` - rădăcina crate-ului: deocamdată doar lint-uri + contractul
  documentat, fără logica SPSC.
- `REQUIREMENTS.md` - 5 cerințe cu id-uri OFT (`req~spsc-...`).
- `README.md` - tabelul de mapare C++ -> Rust.
- `.gitignore` - ignoră `target/` (folderul de build) și `Cargo.lock`.
- `docs/adr/0026-...md` - ADR-ul, în seria ta.

Disciplina e coaptă de la primul pas (ADR + REQ + OFT + porți de lint), nu
retrofitată. Verificat curat: `cargo build`, `cargo clippy -- -D warnings`,
`cargo fmt --check`. Încă necomis (tu faci push, eu git local).

**Unde suntem:** scaffold gata și verde. **Urmează** pasul 2 (structura
`Shared` - ring buffer-ul + head/tail pe cache-line-uri separate) și pasul 3
(`Producer`/`Consumer` + protocolul acquire/release), fiecare cu teste
tag-uite.

## 2026-09-15 - Pasul 2+3: structura + push/pop (verde)

Structura și operațiile le-am făcut împreună, fiindcă în Rust sunt o
unitate: `Shared` fără push/pop = cod nefolosit care sub poarta
`deny(warnings)` nu compilează.

**Partea 1 - depozitare + ownership (partea Rust-specifică):**
- Buffer: `[UnsafeCell<MaybeUninit<T>>; N]`. `MaybeUninit` fiindcă un slot e
  neinițializat până scrie producer-ul (Rust nu-ți dă voie „pe față" la
  memorie neinițializată). `UnsafeCell` = singura cale legală prin care două
  thread-uri ating aceeași memorie printr-o referință partajată.
- `CachePadded` = wrapper `#[repr(align(64))]` peste head/tail, pe
  cache-line-uri separate. Echivalentul `alignas(64)` din C++.
- Capacitate `N` const-generică, cu verificare la compile-time că e putere-a-
  lui-2 și cel puțin 2 (echivalentul `static_assert`).
- `channel()` întoarce `(Producer, Consumer)`, niciunul `Clone` -> contractul
  „un producer, un consumer" e garantat de compilator.
- `unsafe impl Sync for Shared` cu un comentariu `// SAFETY:` care explică de
  ce e sigur (producer-ul scrie doar slot-uri pe care le deține, consumer-ul
  citește doar ce a fost publicat). Singurul `unsafe impl` din cod, justificat.
- `Drop` manual care drenează slot-urile rămase pline când queue-ul moare (în
  Rust eliberezi tu `T`-urile din `MaybeUninit`).

**Partea 2 - protocolul (identic cu C++):**
- `push`: citește `tail` (Relaxed), verifică plin cu `head` (Acquire), scrie
  slot-ul, publică `tail` (Release). Plin -> `Err(item)`, îți dă valoarea
  înapoi (mai curat decât bool + drop din C++).
- `pop`: citește `head` (Relaxed), verifică gol cu `tail` (Acquire), citește
  slot-ul, publică `head` (Release).
- Exact aceeași ordonare acquire/release ca în `SpscQueue.h`.

Verificat verde: `cargo build`, `cargo clippy` (poarta `deny(clippy::all)`,
zero warning nici măcar pedantic), `cargo fmt --check`. Necomis.

**Urmează:** testele (unit + threaded + Loom), apoi Miri (nightly, cer voie
la instalare).

## 2026-09-15 - Teste + Miri + Loom

- **Teste unit + threaded (7, verzi):** gol -> None, plin -> Err la depth
  N-1, ordine FIFO, wrap de indici (mask), head/tail pe cache-line-uri
  separate, Drop drenează slot-urile rămase, și producer/consumer pe
  thread-uri separate (100k elemente, ordine verificată). clippy
  --all-targets clean, fmt clean.
- **Miri: 6/6, zero UB.** Interpretorul Miri a rulat testele unit și n-a
  găsit niciun comportament nedefinit pe blocurile unsafe (write/read
  `MaybeUninit`, acces `UnsafeCell`, Drop). Testul threaded e ignorat sub
  Miri (prea lent; concurența o acoperă Loom).
- **Loom: TRECE pe Linux (verificat prin WSL).** Am pus o mică abstracție de
  cell (`with` / `with_mut`) care sub build normal e std, iar sub `--cfg loom`
  e `UnsafeCell`-ul model-checked al Loom. Testul (producer + consumer, 2
  elemente) a explorat TOATE întrețeserile și a confirmat ordering-ul
  acquire/release.
  - **Windows:** Loom crapă la startup (green-threads cu switch de stack prin
    crate-ul `generator`, limitare cunoscută pe Windows) - deci Loom se rulează
    pe Linux/CI, nu pe Windows.
  - **Lecție:** prima versiune a testului avea un spin loop
    (`while ... rx.pop()`) și Loom l-a respins cu „Model exceeded maximum
    number of branches" (spin-urile cer progres nemărginit, iar Loom explorează
    fiecare pas). Rescris cu pop-uri mărginite + join, fără spin -> trece.

---

## Glosar (fiecare termen: ce e + de ce contează)

- **crate** = un proiect/bibliotecă Rust împachetat (`Cargo.toml` + `src/`).
  De ce contează: e unitatea cu care lucrează Rust, echivalentul unei
  librării CMake din C++. Când zici „crate", zici „proiect Rust".
- **Cargo** = build system + package manager al Rust, la un loc. De ce
  contează: face build, teste, lint și dependențe cu o singură unealtă (la
  C++ ai CMake plus un gestionar separat).
- **Cargo.toml** = manifestul proiectului (nume, ediție, dependențe), în
  TOML. De ce contează: e „rețeta" repo-ului, echivalentul lui
  `CMakeLists.txt`.
- **TOML** = format de configurare simplu, `cheie = valoare` cu `[secțiuni]`.
  De ce contează: ușor de citit/scris de om; rudă cu JSON/YAML, dar mai curat
  pentru config.
- **scaffold** = schela: scheletul gol de fișiere + config, înainte de logica
  grea. De ce contează: pui structura și disciplina (ADR, REQ, lint) de la
  primul pas, nu le retrofitezi.
- **toolchain** = setul de unelte de compilare (compiler + linker +
  librării standard). De ce contează: fără el nu compilezi nimic; pe Windows
  alegerea lui (MSVC vs GNU) decide dacă build-ul merge.
- **rustup** = managerul oficial de toolchain-uri Rust. De ce contează:
  instalează și comută între toolchain-uri (stable, nightly, GNU, MSVC) fără
  bătăi de cap.
- **MSVC vs GNU** = două toolchain-uri Rust pe Windows. De ce contează: MSVC
  cere Visual Studio (linker-ul lui); GNU e self-contained (MinGW). Tu n-ai
  Visual Studio, deci mergem pe GNU.
- **linker** = unealta care leagă bucățile compilate (obiecte + librării)
  într-un program final. De ce contează: e ultimul pas al build-ului; dacă
  lipsește (MSVC fără Visual Studio), nu iese executabil.
- **clippy** = linterul Rust. De ce contează: prinde greșeli și cod
  ne-idiomatic; e poarta ta de calitate, ca clang-tidy la C++.
- **rustfmt** = formatorul de cod Rust. De ce contează: format uniform
  automat, zero certuri de stil (ca clang-format).
- **Loom** = unealtă de test care încearcă TOATE ordinile posibile de rulare
  a thread-urilor, una câte una, fără să sară vreuna. De ce contează: prinde
  bug-uri de concurență care apar doar în ordini rare, mai puternic decât
  TSan (care vede doar ce s-a întâmplat la acea rulare).
- **Miri** = interpretor Rust care prinde comportament nedefinit (UB) pe cod
  unsafe. De ce contează: e plasa ta de siguranță pe blocurile `unsafe`,
  echivalentul ASan/UBSan.
- **C ABI** = „priza" binară standard prin care cod din două limbaje se
  apelează reciproc. De ce contează: e felul în care HMI-ul C++ va apela
  codul Rust la Faza 2 (se scrie atunci, nu acum).
- **SPSC** = single-producer/single-consumer: exact un thread scrie, exact un
  thread citește. De ce contează: sub regula asta queue-ul nu are nevoie de
  mutex, doar de două atomice.
- **lock-free** = coordonare fără mutex, prin operații atomice. De ce
  contează: producer-ul nu se blochează niciodată, ideal pentru un thread
  latency-sensitive (ex. poll-ul Modbus).
- **memory ordering (Relaxed / Acquire / Release)** = cât de strict trebuie
  sincronizate operațiile atomice între thread-uri. Folosim toate trei, și de
  ce contează pe cazul nostru:
  - **Release** la publicare (scriitorul își avansează indexul): garantează
    că scrierea în slot e vizibilă ÎNAINTE ca celălalt thread să vadă indexul
    nou.
  - **Acquire** la citirea indexului CELUILALT thread: garantează că vezi și
    slotul scris înainte de Release. Perechea Release -> Acquire e ce previne
    data race.
  - **Relaxed** la citirea indexului PROPRIU (producer citește tail, consumer
    citește head): atomic, dar fără garanții de ordonare - și e ok, fiindcă
    doar thread-ul ăla își scrie indexul, nu e nimic de sincronizat acolo.
    Alegi cel mai ieftin ordering acolo unde nu ai nevoie de mai mult.
- **cache line** = când procesorul citește din RAM nu ia un singur octet, ia
  un bloc întreg (de obicei 64 de octeți) și-l pune în memoria lui rapidă
  (cache). Blocul ăla e un „cache line". De ce contează: dacă două thread-uri
  scriu două variabile diferite care nimeresc pe ACELAȘI bloc de 64B, fiecare
  scriere a unui thread invalidează copia celuilalt și procesorul trebuie să
  resincronizeze - se încetinesc reciproc degeaba (asta e false sharing).
  De-aia punem `head` și `tail` pe cache-line-uri separate.
- **false sharing** = două thread-uri scriu variabile diferite dar de pe
  același cache-line și se încetinesc reciproc. De ce contează: îl eviți
  punând `head`/`tail` pe cache-line-uri separate (`alignas(64)` în C++,
  `CachePadded` în Rust); altfel pierzi performanță degeaba.
- **întrețesere (interleaving)** = ordinea în care se amestecă în timp
  operațiile a două thread-uri. De ce contează: sunt multe ordini posibile și
  bug-urile de concurență se ascund în cele rare. TSan verifică ordinile care
  chiar apar la rulare; Loom le verifică pe toate, una câte una.
- **ASan (AddressSanitizer)** = prinde la rulare erori de memorie: acces în
  afara limitelor, use-after-free, leak-uri. De ce contează: transformă un
  bug de memorie tăcut într-un crash cu raport la prima greșeală.
- **UBSan (UndefinedBehaviorSanitizer)** = prinde comportament nedefinit:
  overflow de întreg cu semn, pointer nealiniat sau null, cast-uri invalide.
  De ce contează: UB-ul e sursa de bug-uri „imposibile"; UBSan îl scoate la
  suprafață.
- **TSan (ThreadSanitizer)** = prinde data race la rulare: două thread-uri
  ating aceeași memorie fără sincronizare, cel puțin unul scrie. De ce
  contează: e poarta ta pe cod concurent (fix ce demonstrezi cu SPSC).
- **Valgrind (memcheck)** = rulează programul într-o mașină virtuală și prinde
  erori de memorie fără recompilare cu sanitizer. De ce contează: prinde ce
  scapă (memorie neinițializată, leak-uri), cu prețul că e mai lent.
- **echivalentul în Rust** = Miri acoperă ASan/UBSan/Valgrind (memorie + UB pe
  cod unsafe); Loom acoperă TSan (data race), dar le verifică pe toate, nu
  doar ce s-a întâmplat la rulare. De ce contează: aceeași disciplină de
  sanitizere din C++-ul tău, mutată pe Rust.
