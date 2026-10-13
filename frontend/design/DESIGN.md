# PayLock Design System — Source of Truth
**Project:** PayLock — A Concurrent Payment Transaction Management System  
**Stitch Project ID:** `14592152822615229882`  
**Design System Name:** High-Velocity Transaction Protocol  
**Baseline Viewport:** 1440px desktop (Primary target), responsive fluid layout  

---

## 1. Color Palette & Tokens

The interface adheres to an engineered dark-mode palette designed for mission-critical concurrency and payment telemetry.

### Surface & Elevation Levels
| Token | Hex Code | Usage |
| :--- | :--- | :--- |
| `background` / `surface` | `#111318` | Base window and document canvas |
| `surface-container-lowest` | `#0C0E13` | Left navigation sidebar background, primary modal backdrops |
| `surface-container-low` | `#1A1B21` | Secondary card containers, metric strips, filter bars |
| `surface-container` | `#1E1F25` | Standard card panels, borders, secondary badges |
| `surface-container-high` | `#282A2F` | Hover states, elevated card segments, active pills |
| `surface-container-highest`| `#33353A` | Tertiary containers, subtle dividers, progress bar troughs |
| `surface-variant` | `#33353A` | High-contrast inner borders |
| `surface-dim` | `#111318` | Backdrop dimmed surface |
| `surface-bright` | `#37393F` | Highlighted borders on focus/hover |

### Text & Contrast (On-Surfaces)
| Token | Hex Code | Usage |
| :--- | :--- | :--- |
| `on-surface` | `#E2E2E9` | Primary headings, prominent data values, active labels |
| `on-surface-variant` | `#BECBB1` | Secondary subtitles, inactive navigation text, table body text |
| `outline` | `#88957D` | Card borders, secondary metadata, unit labels, muted icons |
| `outline-variant` | `#3F4A36` | Subtle inner borders and table grid lines |
| `inverse-surface` | `#E2E2E9` | Tooltips, inverse popovers |
| `inverse-on-surface` | `#2E3036` | Text inside inverted tooltips |

### Brand & Functional Accents
| State / Semantic | Main Token | Value | Container Token | Container Value | On-Color |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Primary (Engine / Active / Success)** | `primary` | `#74EF00` | `primary-container` | `#64D000` | `#163800` / `#245300` |
| **Secondary (Thread / DB Balance)** | `secondary` | `#59DE9B` | `secondary-container` | `#00A669` | `#003921` / `#00311C` |
| **Tertiary (Mutex / Contention / Cycles)** | `tertiary` | `#DCCCFF` | `tertiary-container` | `#C3ABFF` | `#3C0091` / `#571AC0` |
| **Error (Deadlock / Abort / Rollback)** | `error` | `#FFB4AB` | `error-container` | `#93000A` | `#690005` / `#FFDAD6` |

---

## 2. Typography

All typography is strictly paired between prose/headings and monospace technical data.

### Font Families
- **Display, Headings & Body:** `'Geist'`, system-ui, -apple-system, BlinkMacSystemFont, sans-serif
- **Code, Hashes, Currency, Identifiers & Badges:** `'JetBrains Mono'`, monospace

### Type Scale
| Token | Font Family | Size / Line Height | Weight | Tracking / Letter Spacing |
| :--- | :--- | :--- | :--- | :--- |
| `headline-xl` | Geist | 32px / 40px | 600 (SemiBold) | -0.025em |
| `headline-lg` | Geist | 24px / 32px | 600 (SemiBold) | -0.02em |
| `headline-md` | Geist | 20px / 28px | 500 (Medium) | -0.015em |
| `headline-sm` | Geist | 16px / 24px | 500 (Medium) | -0.01em |
| `body-lg` | Geist | 16px / 24px | 400 (Regular) | Normal |
| `body-md` | Geist | 14px / 20px | 400 (Regular) | Normal |
| `body-sm` | Geist | 13px / 18px | 400 (Regular) | Normal |
| `label-md` | Geist | 12px / 16px | 500 (Medium) | +0.02em |
| `label-caps` | JetBrains Mono | 10px / 14px | 600 (SemiBold) | +0.08em (Uppercase) |
| `code-lg` | JetBrains Mono | 14px / 22px | 400 (Regular) | Normal |
| `code-md` | JetBrains Mono | 12px / 18px | 400 (Regular) | Normal |
| `code-sm` | JetBrains Mono | 11px / 16px | 500 (Medium) | Normal |

---

## 3. Layout Geometry & Dimensions

- **Sidebar Width:** Fixed `256px` (`w-64`), full viewport height (`h-screen`), fixed positioning (`left-0, top-0, z-50`).
- **Header Height:** Fixed `56px` (`h-14`), offset by `pl-64`, fixed top positioning (`z-40`), backdrop blur `90%` opacity.
- **Main Content Offset:** `pl-64` (left margin for sidebar), `pt-14` (top margin for header).
- **Container Padding:** `px-6` (1.5rem / 24px), `py-6` (1.5rem / 24px).
- **Border Radii:**
  - Micro / Default (`rounded-DEFAULT`): `2px` (`0.125rem`) — used for buttons, code chips, badges.
  - Large (`rounded-lg`): `4px` (`0.25rem`).
  - Extra Large (`rounded-xl`): `8px` (`0.5rem`) — used for cards, metric panels, modals.
  - Full (`rounded-full`): `12px` (`0.75rem`) — used for status dots, circular pills.

---

## 4. Component Patterns

### A. Sidebar Navigation
- **Brand Header:** PAYLOCK emblem + bold title with pulse indicator `C++ v2.4`.
- **Primary CTA:** High-contrast `New Transaction` button (`bg-primary-container text-on-primary-container hover:bg-primary`) with keyboard shortcut `N`.
- **Navigation Groups:**
  1. *Core Pipeline:* Overview (`⌘1`), Transactions (`⌘2`), Txn Inspector (`⌘3`), Accounts (`⌘4`).
  2. *Concurrency & Locks:* Thread Pool (`⌘5`), Lock Manager (`⌘6`), Deadlock Monitor (`badge: 1`).
  3. *Diagnostics & Runtime:* OS Resources, System Logs, Analytics, Engine Health.
- **Footer Telemetry Box:** Real-time stats (Active Workers, Active Mutexes, Unix IPC Socket) + engineer profile (`NR - Nitin Rawat`).

### B. Top Command Header
- **Breadcrumb:** Path indicator (`PAYLOCK / ENGINE-CORE-01`).
- **Cluster Status:** Live indicator (`CLUSTER: STABLE • 4.2k TPS`).
- **Global Search:** Input with hotkey chip (`⌘K`).
- **Actions:** Simulation toggle (`Simulate`), Notification bell with unread badge, profile badge.

### C. Bento Metric Strip (8 KPI Cards)
- Dense 8-column grid (`grid-cols-2 md:grid-cols-4 lg:grid-cols-8`).
- Individual metric cards:
  1. *Total Txn:* Live count with sparkline trend.
  2. *Committed:* Success percentage bar.
  3. *Rolled Back:* Abort/rollback count with error progress bar.
  4. *Pthreads:* Running vs. waiting thread breakdown.
  5. *2PL Locks:* Exclusive vs. shared lock counts.
  6. *Deadlocks:* Detected cycles count and victim state.
  7. *Memory/Paging:* Simulated frame allocation & hit ratio.
  8. *Avg Latency:* P99 / execution duration microsecond scale.

### D. Data Tables & Status Badges
- High-density rows with `JetBrains Mono` for transaction hashes (`TXN-10485`), accounts (`ACC-101`), timestamps, and numeric amounts.
- Status badges:
  - `COMMITTED`: Solid green dot + `#74EF00` text on translucent `#74EF0015` bg.
  - `PROCESSING`: Pulsing cyan/green dot.
  - `WAITING`: Amber/violet dot + `#DCCCFF` text.
  - `ROLLED_BACK`: `#FFB4AB` text on `#93000A25` bg.
  - `DEADLOCK`: Pulsing red dot + `#FFB4AB` text on error-container.

### E. Modals & Interactive Overlays
- Modal dialog with fixed backdrop blur (`rgba(12, 14, 19, 0.8)`).
- Card container with `rounded-xl`, border `surface-container-high`, header, form controls, and validation error banner.
