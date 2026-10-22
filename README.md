# Arbor

Arbor is a graphing calculator for 2D, 3D, complex functions and changes of variables, in the spirit of Desmos and Math3D. It's built with [Grove](https://github.com/ThatNewCoderJSCpp/Grove) (the window, in Grove markup and Bark styles), [Walnut](https://github.com/ThatNewCoderJSCpp/Walnut) (everything you click on) and [Fizmo](https://github.com/ThatNewCoderJSCpp/Fizmo) (the high-precision numbers).

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/bin/arbor
```

CMake fetches Grove, which brings Fizmo and Walnut with it. Arbor needs Grove **v0.1.41** or newer (`<canvas>` came in v0.1.39, building on machines without a static C++ library in v0.1.40, and `<input_math>`, the live math input, in v0.1.41). The build compiles the window's script once and puts it inside `arbor`, so Arbor opens without compiling anything.

| Target | |
|---|---|
| `arbor` | the app, with its window built in |
| `arbor-host` | the app without the built-in window; it reads the UI folder |
| `arbor-render` | draws a saved graph, or any built-in example, to a PNG without a window: `arbor-render --example torus -o torus.png`, `arbor-render --examples pictures/`, `arbor-render --list` |
| `arbor_tests` | the engine tests (`ctest --test-dir build`); turn them off with `-DARBOR_BUILD_TESTS=OFF` |

`ARBOR_DATA_DIR` keeps graphs and settings somewhere else, and `ARBOR_UI_DIR` runs a UI folder instead of the built-in window.

## The built-in window

Every build makes `build/bin/arbor` with its window and compiled Walnut script built in (`arbor-host --bundle` makes it), so Arbor opens straight away instead of compiling its script on first launch. With `ARBOR_UI_DIR` set it reads that UI folder instead, which is handy for trying UI changes without rebuilding; the script is then compiled on first start and cached. `arbor --check` loads the window and script without opening anything.

## Releases

Pushing a `v*` tag runs `.github/workflows/release.yml`, which builds, tests and packages Arbor for Linux (x86_64, arm64) and Windows (x86_64 as `windows-ucrt64`, i686, and ARM64 as `windows-clangarm64`) and attaches the packages to the GitHub release. The Windows builds don't use MSYS2: Linux cross-compiles them with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (`cmake/llvm-mingw.cmake`, with `-DARBOR_BUNDLE=OFF`) and runs the x86_64 tests under Wine, then a real Windows machine (x86_64 or ARM) runs the tests, bundles the window and its compiled script into `arbor.exe` with the `-with-compiler` package of the Walnut release `ARBOR_WALNUT_TAG` names, and packages it. The tag has to match the version in `CMakeLists.txt`.

`-DARBOR_PACKAGE=ON` is what the workflow uses: it links the C++ runtime statically (into the app and its built-in script) so the packaged app runs on machines without a compiler. On Linux that needs the static C++ library, which Fedora keeps in `libstdc++-static`; ordinary builds don't need it. `cpack` then makes `Arbor-<version>-<platform>.tar.gz` (or `.zip` on Windows) with `arbor`, `arbor-render` and this README. 

## Using it

The home page has your saved graphs, each with a picture of how it looked, then **New graph**, split into sections. Pick a section, then a kind of graph or one of the examples. **Library** (on the home page and above every graph) lists everything you can use.

| Section | Kinds |
|---|---|
| **2D** | `y = f(x)` (and `x = g(y)`, and inequalities), a curve rotated by an angle about a point, implicit curves `f(x, y) = g(x, y)` and `f(x, y) = n` and regions like `x² + y² < 4`, polar `r = f(θ)` and also `f(r, θ) = g(r, θ)` and polar regions like `r < 1 + cos 3θ`, parametric `⟨x(t), y(t)⟩`, a function along a curve `f(x(t), y(t))`, vector fields, contour maps, slope fields, differential equations of any order, points |
| **3D** | surfaces `z = f(x, y)`, surfaces of revolution (a 2D curve turned about the x-axis, the y-axis or any line in its plane), implicit surfaces `f(x, y, z) = g(x, y, z)` and `= n` and solid regions like `x² + y² - z² < 1`, space curves, parametric surfaces (manifolds: tori, Möbius strips, ...), spherical `ρ = f(θ, φ)` and also `f(ρ, θ, φ) = g(ρ, θ, φ)` and spherical regions, cylindrical `r = f(θ, z)` and also `f(r, θ, z) = g(r, θ, z)` and cylindrical regions, about the z, x or y axis (with x or y the equation uses that letter, like `r = 1 + cos(3x)`), *Implicit cylindrical* for any equation or inequality in r, θ and the height along the z, x or y axis, *ρ, r and θ* for surfaces and regions that mix the distance from the origin ρ, the distance from the axis r and the angle θ (and φ and the height), like `ρ r = 2 + 0.5cos(3θ)` or `ρ < 1 + r/2`, a function along a space curve `f(x(t), y(t), z(t))`, vector fields, points |
| **Regions** | areas and solids from any functions: *Region from conditions* (any conditions in x, y, r and θ joined with `and`, `or`, `not` or commas, like `y > x² - 2, x² + y² < 9`, shaded and outlined, with each condition's curve and the area in view), *Region between curves* (y from f(x) to g(x), or x from f(y) to g(y), from where the curves cross or between ends you give, with the area integral, the centroid and slices), *Polar region* (r from f(θ) to g(θ), area ½∫(g² - f²) dθ), *Solid from conditions* (conditions in x, y, z, r, ρ, θ and φ, shaded on the walls of the box, with its edge surface and the volume in the box) and *Solid between surfaces* (z from f(x, y) to g(x, y) over x from a to b and y from h₁(x) to h₂(x), with top, bottom, walls, the base on the floor and the volume as an iterated integral) |
| **Complex** | color maps (domain coloring) with poles and residues, conformal maps, modulus surfaces, complex-valued curves (a real input t on one axis, left-right or up-down, and the complex output f(t) in the plane across it, with that plane drawn at t₀), Riemann surfaces |
| **Transforms** | changes of variables `(x, y) = T(u, v)` and `(x, y, z) = T(u, v, w)` with the mapped grid, the Jacobian matrix and its determinant |
| **PDEs** | partial differential equations in x, in x and y, or in x, y and z, with one, two or three unknown functions u, v, w picked from dropdowns: *Time evolution* (any F(t, x, u, u_t, u_tt, u_x, u_xt, …) = 0, not just u_t = …, including mixed derivatives like u_xxt and up to fourth derivatives in each space variable, from starting values at any starting time, drawn as u(x, t) over x and time, as u(x, y) at t₀, or as level surfaces and colored slices of u(x, y, z)), *Steady state* (Laplace, Poisson, beams, minimal surfaces and other boundary value problems with no time) and *Marching in x* (solving along x from u, u_x, … given along a line, for wave-like equations). Each edge takes its own conditions, like `u = sin(t)`, `u_x = 0`, `u_x + u = 0` or two conditions for fourth-order equations, or the words insulated, periodic or free. Every time step is implicit (Crank–Nicolson, BDF2 or backward Euler): Newton's method solves the whole grid at once, with a banded LU or GMRES with ILU(0) for the sparse linear systems |
| **Misc** | rules and patterns built from a condition, what to do when it's true and what to do when it's false: piecewise functions, sequences like 3k + 1 (Collatz), escape-time sets like the Mandelbrot and Julia sets, and Langton's ant; and the brachistochrone, the cycloid from A to B that a bead slides down fastest, raced against the straight line and any curve y = f(x) you give, with the rolling circle that draws it and the times |

A graph holds any number of items of its own dimension, added with **+ Add to graph**. Every item has its options as checkboxes: derivatives, tangent and normal lines and vectors, osculating circles and planes, curvature, torsion (twist), the Frenet frame, contours, contour maps on the floor, traces at `x = c` and `y = c`, gradient arrows, tangent planes, Gaussian and mean curvature, divergence and curl, flow lines, |f| rings and phase lines, Jacobian shading and the Jacobian of one grid cell. The values (curvature, κ, τ, J, det J, ∇f, ...) are listed under the item, each with its formula when Arbor can write one: T(t), N(t), B(t), κ(t), τ(t), the radius and center of the osculating circle, the osculating plane, the tangent line, f′, ∇f, div and curl, and the volume and surface-area integrals. **Symbols** and **Values** above the graph pick what the readouts show: formulas, numbers, or both. Formulas are tidied (sin² + cos² = 1, constant parts folded to one number) and checked against the original at a few points before they're shown.

**Typing.** Equations are drawn as math inside their box while you type, the way they'd look on paper, and typing builds the structure for you: `^` starts a power and everything you type stays up there until you press →; `_` does the same for a subscript; `/` turns the term before it into a numerator and puts you in the denominator (→ to leave); `sqrt` and `cbrt` make a root with you under it. Brackets you type are always kept and drawn, so `y^(4)` shows as y⁽⁴⁾. Backspace takes these apart cleanly: at the start of a power it removes the power and keeps what was in it, at the start of a denominator it turns the fraction back into what was on top followed by what was below. Brackets grow around tall things, and `<=`, `!=`, `pi` and `alpha` become ≤, ≠, π and α. Expressions are written the way you'd write them by hand: `2x sin x`, `sin^2 x`, `|x|`, `√x`, `x²`, `3!`, `e^x`, `θ` or `theta`, `<cos t, sin t>`. A **Definition** item makes a function (`f(x) = x^2 + 1`) or a number (`a = 2`, `rate = 0.5`) every other item can use. A number gets a slider with a range and step, and ▶ plays it. A name you use without defining it shows up at the top of the list with a button that adds a slider for it. θ, φ and ρ can be sliders too (say, a rotation by `theta`); items that use them as coordinates, like polar curves and spherical surfaces, keep their own θ, φ or ρ and ignore the slider. The small boxes under an equation (angles, ranges, t₀ and the like) are math boxes too, so `theta` or `pi/6` shows as θ or π/6.

**Functions and constants.** Besides the usual ones (sin, cos, ..., exp, ln, log, sqrt, abs, floor, gamma, erf), Arbor knows the Riemann zeta function `zeta(s)` (and the Hurwitz zeta `zeta(s, a)`), the Lambert W function `lambertw(x)` and its other branch `lambertw(x, -1)` (also written `LambertW` or `ProductLog`), the digamma, trigamma and polygamma functions, beta, binomial, Pochhammer, harmonic numbers, the incomplete gamma functions, the Dirichlet eta and beta functions, Riemann ξ, polylogarithms, the Clausen function, erfc, erfi, the inverse error functions, the sine, cosine, exponential and logarithmic integrals, Fresnel integrals, the Airy functions Ai and Bi, complete and incomplete elliptic integrals and the Jacobi elliptic functions. Also:

- all six inverse hyperbolic functions (`asinh`, `acosh`, `atanh`, `asech`, `acsch`, `acoth`, also written `arcsech`, `arsech`, ...), the inverse Gudermannian `invgd`, and `nsinc` (sin πx / πx)
- `log(x, b)`, the base-b logarithm (the base comes second, as in Fizmo and Walnut)
- `InverseSinc(x)` (also `invsinc`) and `LambertW(x)` / `lambertw(x)`, which show their full names in the equation instead of a one-letter W
- `mag(z)` and `mag2(z)`, the magnitude and squared magnitude of a complex number
- `lagamma(x)` = ln |Γ(x)|, next to `lgamma(x)` = ln Γ(x), which is undefined in real graphs where Γ(x) < 0
- `clausen(θ)` (the Clausen integral Cl₂, also `clausenintegral`) and `clausen(θ, s)`, the Clausen function Clₛ
- `Ei(x, n)` = Eₙ(x) and `li(x, n)` = liₙ(x), the generalized exponential and logarithmic integrals
- the Scorer functions `Hi(x)` and `Gi(x)`
- all twelve Jacobi functions (`sn cn dn cd cs dc ds nc nd ns sc sd`), `ellipticPi(φ, n, m)`, and Carlson's `carlsonRF`, `carlsonRD`, `carlsonRJ`, `carlsonRC`, `carlsonRG`
- the number sequences `bernoulli(n)` (Bₙ), `eulernumber(n)` (Eₙ), `tangentnumber`, `secantnumber`, `eulerian(n, k)`, `stieltjes(n)` (γₙ), `metallic(n)` (σₙ), `invmetallic`, `metallicmean`, `beraha`, and `fibonacci(x)` / `lucas(x)` (for any real x, or the polynomials with `fibonacci(x, n)`)
- the polynomials `chebyshevT/U/V/W(x, n)` and `legendreP/Q(x, n)`
- `trilog`, `spence`, `rogersL`, `dirichletlambda`, `riemannchi`, `legendrechi`, `lerch`, `debye`, `betainc`, `betareg`, `betaregc`, `invbetareg`, `invgammaP`, `invgammaQ`, `falling`, `lbeta` and `inverfi`

Sequences and polynomials are written with subscripts (B₂, T₃(x), γ₁, F₁₀). New functions take their inputs in Fizmo's order, the variable first.

Constants: `pi`, `tau`, `e`, `golden` (φ), `psi` (ψ = (√5 - 1)/2 = 1/φ), `eulergamma` (the Euler-Mascheroni γ), `catalan`, `apery` (ζ(3)), `silver`, `silverconstant`, `bronze`, `inf` and `i`. In the long number types they're computed to the full precision of the type. **Library** shows all of them with what each input does; clicking a name puts it into the equation you were last typing in. Any other word, like `alpha` or `speed`, is a variable that can get a slider.

**Conditions.** `if(condition, a, b)` is a when the condition holds and b otherwise; leave b out to draw only where it holds. Conditions use `<`, `<=`, `>`, `>=`, `=`, `!=`, chains like `0 < x <= 2`, and `and`, `or` and `not`; `a mod b` is the remainder.

**Where things are drawn.** Under `y = f(x)`, implicit curves and regions, surfaces and implicit surfaces there's a row like `[ -∞ , ∞ ]` for each variable: type numbers to draw only between them, and click a bracket to switch between including that end `[` and leaving it out `(`. Ends of a function's domain get a solid dot when they're included and a hollow one when they aren't. Polar and parametric curves have the same brackets on their θ or t range. Empty boxes draw as far as the graph goes. The **Bounds** button above the graph sets how much of the plane is drawn (in 2D the rest is dimmed and framed) or the size of the 3D box (x, y and z from and to; leave z empty to fit it to what's drawn). Settings has the bounds new graphs start with.

**Differential equations.** The *Differential equation* kind takes any order: `y' = x - y`, `y'' = -sin(y) - 0.15y'`, `y''' + y' = sin(x)`, `y^(4) = y`, written with primes or `y^(n)`. It can also be implicit, like `(y')^2 + y^2 = 1`: the highest derivative is solved for numerically at each step. Give x₀ and the starting values y(x₀), y′(x₀), ... separated by commas; the solution runs both ways from x₀ with the classical Runge-Kutta method. It can draw y′, y″ and y‴ and the phase plane (y, y′), and reads them all at a point you choose.

**Misc.** *Piecewise function*: a condition, the formula where it holds and the one where it doesn't. *Conditional sequence*: a number a goes through one of two rules each step depending on a condition on a and the step k, like `a mod 2 = 0` → `a/2`, else `3a + 1`, with a start, a step count and an optional stop condition; the terms are plotted against k. *Escape-time set*: z ↦ f(z, c) is repeated for every point while a condition holds (`|z| <= 2`), and the point is colored by how many steps it lasted; points are c (Mandelbrot) or, with Julia on, the starting z for a fixed c. *Langton's ant*: an ant on a grid checks a condition on the cell under it (`cell = 0`, and it can use the step k and its position x, y) and follows one list of actions or the other: `R`/`L` turn, `U` turn around, `N` no turn, `F` flip the cell, `S2` set it to 2, `M` move. `R F M` / `L F M` is Langton's ant, which builds its highway after about 10,000 steps.

**Turning and rotating curves.** *Surface of revolution* (3D) turns `y = f(x)`, `x = g(y)` or `⟨x(t), y(t)⟩` about `x-axis`, `y-axis`, `y = c`, `x = c` or any line `y = m x + b`, through a full turn or any angle. It shows the parametrization r(t, θ), the distance from the axis, the volume and the surface area (Pappus) with their integrals, with the curve and the axis drawn in; the 2D graph's plane is stood upright, so its y becomes z. The range is written like a domain, `[1, ∞)`: click a bracket to include or leave out that end (an open end draws its rim dashed), and leave a box empty or write `inf` / `-inf` for no end. With an endless range the surface is drawn as far as the view reaches and the volume and area are improper integrals, so `y = 1/x` on `[1, ∞)` (Gabriel's horn) reads volume π and surface area ∞. A range that runs into a pole, like `1/x` on `(0, 4]`, is reported as diverging rather than as a large number. *Direction arrows on the curve* shows which way t runs. *Rotated curve* (2D) turns a curve about a point by an angle (radians, or degrees with Degrees on), draws it over the original, and lists the rotated parametrization and the rotated curve's equation.

**When something is slow or stuck.** Points where a function isn't defined (ln of a negative number in a real graph, a pole, an overflow) are skipped. Each item gets 20 seconds to draw; one that takes longer is stopped with a note saying so, and the rest of the graph still draws. After 5 seconds a bar above the items says Arbor is still drawing, with **Stop drawing** and **Quit Arbor**. If the window itself stops responding for 8 seconds, Arbor asks whether to end it, with Fizmo's own message box (the system one on Windows and macOS; on Linux zenity or kdialog when the desktop has them, otherwise a plain X11 box), since the window that's stuck can't show anything itself; the graph is kept and offered back the next time it opens. Closing Arbor never waits more than 3 seconds for a drawing to stop.

**Colors.** Click an item's color dot for a row of swatches, or pick any color.

**Endless parameter ranges.** Parametric curves and space curves take the same kind of range: `t from (-∞, ∞)` with both boxes empty draws the whole curve (it's sampled through a change of variable, so the far ends are reached), and the t₀ slider gets a sensible span around t₀. The arc length readout on space curves works from an endless start too. *Direction arrows* puts small arrows along either kind of curve, pointing the way t increases.

**The point t₀.** Items with a point on them (t₀ on curves, θ₀ on polar curves, x₀ on functions) get a slider under them that moves it from the start of the range to the end, so the tangent, normal, curvature, Frenet frame and readouts can be read anywhere along the curve. ▶ sweeps it along on its own. The slider takes over when t₀ is a number; when t₀ is the name of a slider, that slider moves it instead.

**Branches.** When an expression uses a function with more than one value (`sqrt`, roots, `ln` and `log` in the complex plane, the inverse trig functions, fractional powers like `z^(1/3)`), a small **branch k** box appears in the corner of its input. 0 is the principal branch; 1, 2, -1, ... are the others: `sqrt` on branch 1 is the negative root, `ln` on branch k adds 2πki, `arcsin` on branch k is πk ± the principal value, and so on.

**Poles and residues.** A complex color map finds the poles in view (by the argument principle on a grid, then Newton's method on each one) and labels each with its residue, computed as a contour integral around it. The list under the item gives each pole, its order and Res f. A pole beside a branch cut, where the Laurent series doesn't exist, gets a ≈. The modulus surface can list them too.

**The view.** In 2D, drag to move and use the wheel to zoom around the mouse (Shift+wheel zooms y only). In 3D, drag to turn, Shift+drag or right-drag to move, wheel to zoom, Shift+wheel to widen or narrow the domain. Double-click resets the view. The bar above the graph has grid, axes, labels, degrees, perspective and equal-axes switches, Top and Front views, **Bounds**, **Reset** and **Export** (a PNG twice the window's size).

**Saving.** **Save** keeps the graph with a thumbnail. Closing a graph with changes (going back to the graph list, opening another graph or closing the window) asks whether to save them, or always saves, or always throws them away: that's a setting. If Arbor quits while a graph has unsaved changes, the home page offers them back the next time. Undo and redo are in the top bar.

## Numbers: speed and precision

Arbor computes values in Walnut's software floats from Fizmo, picked in Settings or in the top bar at any time. Walnut's doubles are two steps wider than its floats of the same name, so some widths have two names:

| Type | Bits | Digits | `sin(x)·x + exp(-x²/4)` per point | `x³ - 2x + 1` per point |
|---|---|---|---|---|
| `short short float` | 32 | ~7 | ~0.3 µs | ~0.15 µs |
| `short float` | 64 | ~16 | ~0.9 µs | ~0.15 µs |
| `float` = `short short double` | 128 | ~34 | ~8 µs | ~1 µs |
| `long float` = `short double` | 256 | ~71 | ~40 µs | ~1.2 µs |
| `long long float` = `double` | 512 | ~148 | ~75 µs | ~3 µs |
| `long double` | 1024 | ~301 | ~0.2 ms | ~8 µs |
| `long long double` | 2048 | ~607 | ~0.75 ms | ~20 µs |

For comparison, the CPU's own 64-bit double does either in about 0.05 µs. Fizmo computes functions like sin and exp for `short short float` on the CPU's double, for `short float` with double-double arithmetic and, for the functions where it's faster, for `float` with quad-double arithmetic; the wider types go through BigFloat. So:

- **Drawing** always happens with the CPU's double first, so the graph appears at once and follows the mouse.
- **Values** (every readout: f(x₀), derivatives, curvature, torsion, Jacobians, gradients, tangent planes, special functions and constants) are computed in the type you picked. With `long long double` you get about 600 digits.
- **Drawing again precisely** is a switch in Settings: once the fast drawing is up, curves and surfaces are drawn a second time in the picked type in the background, and replace the fast ones when they're done. Color maps and shaded regions stay in double, since a pixel can't show the difference. Changing anything cancels a slow pass right away.
- **Measure speed on this computer** in Settings times every type on your machine.

## Settings

Dark, Light or your own colors (background, panels, text, accent), interface size, the number type, the precise second pass, what happens to unsaved changes, detail (Fast, Balanced, Fine), smooth 3D, and the drawing bounds new graphs start with (2D limits, the 3D box). Settings and graphs live in the data folder shown under About (`~/.local/share/arbor` on Linux, `%APPDATA%\Arbor` on Windows, `~/Library/Application Support/Arbor` on macOS). Graphs are plain Bark files, so they can be read, diffed and shared.

## How it's put together

| Folder | |
|---|---|
| `Source/expr.cpp`, `tidy.cpp` | the expression parser (implicit multiplication, function definitions, vectors, relations, conditions), symbolic derivatives and simplification |
| `Source/library.cpp` | the catalog of functions, constants and variables shown in Library |
| `Source/special_double.hpp`, `fizmo_num.hpp` | special functions in double and complex double (zeta, Lambert W, gamma, digamma, ...), and every function and constant in the Fizmo types |
| `Source/program.cpp`, `evaluator*.cpp`, `eval_impl.hpp` | expressions compiled to a small stack machine, run in batches over `double`, complex `double`, and every Fizmo float |
| `Source/build*.cpp`, `scene.cpp` | one builder per kind of item: sampling, adaptive curves, marching squares and tetrahedra, meshes, analysis; `build_misc.cpp` has the differential equations and the Misc section |
| `Source/render.cpp` | the software renderer: anti-aliased lines and text, a z-buffered, lit 3D rasterizer with supersampling |
| `Source/graph.cpp`, `kinds.cpp`, `examples.cpp` | the graph model, the catalog of kinds, Bark save files, the examples |
| `Tools/arbor.cpp` | the app host: the Grove document, the background builder thread, the canvas and mouse, and the `arbor.*` functions the script calls |
| `UI/arbor.grove`, `arbor.bark`, `arbor.wal` | the window: markup, styles and the Walnut controller |

## Branches

- `main` is what's released. Every release is a `v*` tag on `main`.
- `dev` is where work happens. Push to `dev` (or open pull requests into it), and merge `dev` into `main` when it's ready to release.
