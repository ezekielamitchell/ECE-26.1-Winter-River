# Winter River — Trainee Assessment

**A short, hands-on check after the intro data center workshop.**
Use this right after a 30–60 minute "what is a data center" session. It works
entirely on the Winter River tabletop and assumes **no technical background** —
trainees only look at the module screens and answer in plain language.

- **Time:** ~20–30 minutes
- **Format:** instructor drives the system; trainee observes and writes answers
- **Pass mark:** 7 of 10 core points (Parts 1–3 and 5), plus completing Part 4
- **You'll need:** the tabletop powered on with all modules showing a status,
  one printed copy per trainee, and the instructor laptop/Pi for the live demos

> **Each module has a small screen (OLED).** It shows the module's **name** on
> top and its **status word in brackets**, e.g. `utility_a [GRID_OK]` or
> `rack_a1 [NORMAL]`. "Read the screen" always means read that status word.

---

## Trainee

```
Name: ______________________________   Date: ____________

Workshop instructor: ___________________   Score: ______ / 10
```

---

## Part 1 — Name That Module  (5 points)

Walk up to the board. Draw a line (or write the letter) matching each **module
name** to **what it does** in everyday words.

| Module on the board        | What it does (write the letter) |
|----------------------------|---------------------------------|
| 1. `utility`               | ____ |
| 2. `transformer`           | ____ |
| 3. `generator`             | ____ |
| 4. `ups`                   | ____ |
| 5. `cooling`               | ____ |
| 6. `server_rack`           | ____ |

**Choices:**
- **A.** The computers that do the actual work (the reason the building exists)
- **B.** Power coming in from the electric company / the grid
- **C.** A backup engine that makes power when the grid goes out
- **D.** Fans that keep the room from overheating
- **E.** A battery that keeps power on for a few seconds during a switchover
- **F.** Changes the voltage to a safer/usable level ("steps it down")

*(Score: 1 point each, any 5 correct = full marks.)*

---

## Part 2 — Follow the Power  (2 points)

On **Side A** of the board, start at the grid and trace the power with your
finger all the way to the servers. Number these stops **1 to 5** in the order
the power reaches them:

```
____  ups_a            (battery backup)
____  utility_a        (grid coming in)
____  server_rack_a1   (the computers)
____  generator_a / lv_switchgear_a   (backup engine + the switch that picks grid-or-backup)
____  transformer_a    (changes the voltage)
```

- **2 pts:** all 5 in the right order  •  **1 pt:** start (`utility_a`) and end
  (`server_rack_a1`) correct

---

## Part 3 — Two Sides for Safety  (3 points)

1. How many complete, independent power paths does this data center have?
   (Hint: look at the labels ending in `_a` vs `_b`.)

   `______________________`  **(1 pt)**

2. **Why** build two of everything instead of one? Answer in one sentence.

   `__________________________________________________________`  **(1 pt)**

3. True or false: *If Side A completely loses power, Side B keeps its own
   servers running.*   `____________`  **(1 pt)**

---

## Part 4 — Watch It Fail  (hands-on — must complete)

**The instructor runs each demo. You just watch the screens and fill in the
blanks.** (Instructor commands are in the answer key on the last page.)

### Demo 1 — The grid goes out (but we're covered)
The instructor cuts power from the grid on Side A.

- The `utility_a` screen now reads: `________________`
- Watch `generator_a` for about 10 seconds. Its status changes to:
  `____________` → `____________`
- Did the Side A servers (`rack_a1/a2/a3`) **stay running**?  `Yes / No`

> **What you just saw:** the **UPS battery** held the servers up for the few
> seconds it took the **generator** to start. That's the backup chain working.

### Demo 2 — A whole side is lost
The instructor knocks out **both** the grid **and** the generator on Side A.

- The three **Side A** racks (`rack_a1/a2/a3`) now read: `________________`
- The three **Side B** racks (`rack_b1/b2/b3`) read: `________________`
- In one sentence, why is Side B still fine?

  `__________________________________________________________`

> **What you just saw:** this is the whole point of "two sides." Losing one
> side does **not** take down the other.

### Demo 3 — Too hot (optional)
The instructor turns off most of the fans on Side A.

- Did the servers **lose power**?  `Yes / No`
- What problem grows instead?  `________________`

> **What you just saw:** not every problem is a *power* problem — keeping the
> room **cool** matters just as much.

*(Instructor: tick when the trainee has observed each demo and recovery is done.)*
`[ ] Demo 1   [ ] Demo 2   [ ] Demo 3 (optional)`

---

## Part 5 — Quick Check  (circle one each — bonus reinforcement)

1. A **UPS** is basically a… **(a)** big fan **(b)** battery **(c)** computer
2. The **generator** turns on when… **(a)** it's hot **(b)** the grid power is lost **(c)** a server is busy
3. If **Side A** fails, what keeps the data center's work going?
   **(a)** Side B **(b)** the cooling fans **(c)** nothing
4. What keeps a data center from overheating?
   **(a)** the UPS **(b)** the generator **(c)** the cooling fans

---

## Done!

```
Trainee passed (7+ / 10 and completed Part 4):   [ ] Yes   [ ] No

Instructor signature: ___________________________
```

<div style="page-break-before: always"></div>

---

# Instructor Answer Key & Demo Script
*(Detach or keep this page — do not give to the trainee.)*

### Before you start
1. Power the tabletop; wait until every module screen shows a status.
2. **Start the simulation engine** so the system reacts automatically (the
   generator starting itself, racks tripping, etc. all come from this):
   ```bash
   cd broker && source venv/bin/activate && python main.py   # leave running
   ```
   Without it, the modules won't respond to each other and the Part 4 demos
   won't cascade on their own.
3. In another terminal on the Pi, confirm a healthy baseline:
   ```bash
   ./scripts/status.sh
   ```
   All 24 nodes should be online, both `ups_a` / `ups_b` `NORMAL`, all 8 racks
   `NORMAL`. The system is safe low-voltage — there is no shock hazard.

### Answers
- **Part 1:** 1→B, 2→F, 3→C, 4→E, 5→D, 6→A
- **Part 2:** `utility_a` =1, `transformer_a` =2, `generator_a / lv_switchgear_a` =3,
  `ups_a` =4, `server_rack_a1` =5.
  *(Full real chain, for reference: utility → HV/MV transformer → MV switchgear
  → MV/LV transformer → LV switchgear → UPS → server rack, with the generator
  tying into the LV switchgear, which is the grid-or-backup transfer point. The
  trainee version is collapsed to 5 stops on purpose.)*
- **Part 3:** (1) **Two** — Side A and Side B. (2) So that if one side fails,
  the other keeps the servers running (redundancy / "2N"). (3) **True.**
- **Part 5:** 1→b, 2→b, 3→a, 4→c

### Demo commands
Run these from the Pi (broker host `192.168.4.1`). Give the system a few seconds
after each command, then **run the matching RECOVER command before moving on.**

**Demo 1 — grid out, backup covers it**
```bash
# TRIGGER
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:OUTAGE VOLT:0"
# Expect: utility_a -> [OUTAGE]; generator_a -> [STARTING] then [RUNNING] (~10 s);
#         racks may blink to [DEGRADED] while the UPS is on battery, then [NORMAL].
# RECOVER
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:STANDBY RPM:0 FUEL:85"
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control" -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
```

**Demo 2 — whole side lost**
```bash
# TRIGGER
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control"   -m "STATUS:OUTAGE VOLT:0"
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:FAULT RPM:0"
# Expect: rack_a1/a2/a3 -> [FAULT] (no power); rack_b1/b2/b3 stay [NORMAL].
# RECOVER
mosquitto_pub -h 192.168.4.1 -t "winter-river/generator_a/control" -m "STATUS:STANDBY RPM:0 FUEL:85"
mosquitto_pub -h 192.168.4.1 -t "winter-river/utility_a/control"   -m "STATUS:GRID_OK VOLT:230.0 FREQ:60.0"
```

**Demo 3 — cooling loss (optional)**
```bash
# TRIGGER
mosquitto_pub -h 192.168.4.1 -t "winter-river/cooling_a/control" -m "FANS_RUNNING:10 STATUS:DEGRADED"
# Expect: racks stay powered ([NORMAL]); the cold/hot-aisle temperature climbs
#         (visible on the cooling screen and in Grafana, if shown).
# RECOVER
mosquitto_pub -h 192.168.4.1 -t "winter-river/cooling_a/control" -m "FANS_RUNNING:55 STATUS:NORMAL SPEED:60 TEMP:65"
```

### After the assessment
Re-run `./scripts/status.sh` and confirm the baseline is healthy again before
the next trainee. Tip: keep a live view open during demos so trainees can watch
states change in real time:
```bash
mosquitto_sub -h 192.168.4.1 -t "winter-river/#" -v
```
