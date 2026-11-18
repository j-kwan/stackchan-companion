#pragma once
// =============================================================================
// fluid.h — particle viscoelastic fluid (Clavet et al. 2005), and nothing else
// =============================================================================
// WHY THIS SOLVER. The alternatives were a cellular sand automaton, which has
// no viscosity to offer beyond a slide probability, and an Eulerian solver,
// whose iterative pressure solve does not fit a 33 ms frame on this chip. The
// double density relaxation used here needs no pressure solve at all, stays
// stable at a large dt, and takes viscosity as an explicit parameter — which
// is the one knob the user asked for by name.
//
// PURE: no Arduino, no M5, no float-to-pixel assumption beyond the world size.
// It is tested natively (test/test_fluid) like engine/ and behavior/ (rule 7).
//
// THE CLAMP IS LOAD-BEARING. Velocity is capped at half an interaction radius
// per step. Without it a violent shake moves a particle further than a wall is
// thick and it leaves the box for good — the failure a user would report as
// "the fluid leaked". With it, the collision step cannot be outrun.
// =============================================================================

#include <stdint.h>
#include <math.h>

namespace sce {
namespace fluid {

// The world IS the screen, in pixels: the splat below is the only place that
// converts, and keeping one unit removes the class of bug where a tuning value
// is right in one space and absurd in the other.
inline constexpr int WORLD_W   = 320;
inline constexpr int WORLD_H   = 240;
inline constexpr int MAX_PARTS = 400;   // LED-FLUID.md: the ceiling P1 must measure
inline constexpr int MIN_PITCH = 8;     // LED-FLUID.md: the frame budget, not a taste
inline constexpr int MAX_CELLS = (WORLD_W / MIN_PITCH) * (WORLD_H / MIN_PITCH);

// THE COST CEILING, and why a neighbour grid alone is not one. The grid bounds
// the pair count only while the particles are SPREAD OUT — and gravity spends
// its whole life doing the opposite. Once a pile fits inside one cell, every
// pair is a neighbour again and the step is O(n^2) after all. On the robot
// that took about ten seconds of sloshing: the step outran its 16 ms period,
// the simulation task stopped yielding, IDLE0 starved, and the task watchdog
// aborted the chip in a boot loop at 20.5 s.
//
// So the neighbours examined per particle are CAPPED. Twenty-four is roughly
// twice a full neighbourhood at rest density, so ordinary fluid never notices;
// a collapsed pile is approximated rather than integrated exactly, which is
// the right trade for a toy whose alternative is not running at all.
inline constexpr int MAX_NEIGH = 24;

// The separation pass has a bound of its own, on candidates EXAMINED rather
// than pairs kept. Its radius is one grain diameter, so at rest a grain has
// about six real neighbours; forty-eight is room for a transient pile without
// letting one grain walk a whole cell list.
//
// IT IS A GUARD, NOT A TUNING KNOB, and the difference was measured: doubling
// it to 96 changes neither the settled speed nor the closest pair by a printed
// digit, at any pitch. Nothing in normal operation reaches it — it is there so
// that a pathological pile cannot make one grain walk an unbounded list, which
// is the failure that boot-looped the chip. Raising it therefore buys nothing,
// and a separation that looks too weak is never short of this.
inline constexpr int MAX_TOUCH = 48;
inline constexpr int SEPARATE_PASSES = 4;

// ---- the fluid, in the only units that let it be checked -------------------
// Every length is a multiple of the dot pitch and every stiffness is an
// acceleration in px/s², so each of these can be compared with the one number
// that decides whether a liquid holds itself up: gravity, 900 px/s² at 100 %.
// They are CALIBRATED, not derived — the settling they produce is measured in
// test_fluid, which is where a change to any of them will be argued.
inline constexpr float GRAIN_DIAM    = 0.90f;   // grain size, in dot pitches
inline constexpr float REST_SPACING  = 1.40f;   // rest gap, in grain diameters
inline constexpr float H_FACTOR      = 1.8f;    // radius, in rest gaps (≈10 neighbours)
inline constexpr float PRESSURE_K    = 4000.0f; // px/s² per unit of density error
inline constexpr float PRESSURE_KNEAR= 2000.0f; // px/s², the anti-clustering term

struct Params {
    float viscosity = 30;    // 0-100   slider
    float gravity   = 100;   // 0-200 % slider
    float bounce    = 25;    // 0-90  % slider
    float gyroGain  = 100;   // 0-200 % slider
    int   particles = 240;   // 60-400
    int   pitch     = 12;    // 8-20 px, continuous (the slider has no steps)
};

// What the renderer consumes. Two bytes per cell: how much matter is there,
// and how fast it moves. The colour is not decided here — look.h owns that,
// so a palette change never touches the solver.
struct DotGrid {
    int     cols = 0, rows = 0;
    uint8_t dens[MAX_CELLS] = {0};
    uint8_t spd [MAX_CELLS] = {0};
};

class Sim {
public:
    void setParams(const Params& p) {
        _p = p;
        if (_p.pitch < MIN_PITCH) _p.pitch = MIN_PITCH;

        // ONE PARTICLE IS ONE DOT, and every length below is a multiple of that
        // one. A particle is a grain the size of a lit LED: its diameter IS the
        // dot pitch, it rests a quarter of a diameter clear of its neighbours,
        // and it interacts out to about two diameters.
        //
        // The radius used to be 2.2 × pitch with a rest density of its own, and
        // that was two mistakes in one line. Tying the interaction radius to the
        // dot size let the RENDER slider retune the physics; and since the rest
        // density was a constant, the number of neighbours inside the radius
        // grew with the particle slider until, at 400 particles, an ordinary
        // resting fluid already sat at MAX_NEIGH — so the cost ceiling truncated
        // normal fluid, every particle computed its pressure from a different
        // number of neighbours, and the fluid boiled instead of settling.
        _d    = GRAIN_DIAM * (float)_p.pitch; // a grain is a dot
        _s    = REST_SPACING * _d;            // and rests this far from the next
        _h    = H_FACTOR * _s;
        _rho0 = 3.14159265f * H_FACTOR * H_FACTOR / 6.0f;   // ∫(1-r/h)² at spacing _s
        _vmax = 0.5f * _h;                    // the clamp, per step

        // YOU CANNOT ASK FOR MORE GRAINS THAN THERE ARE DOTS. Non-penetration
        // is a promise the solver keeps by pushing, and pushing cannot create
        // room that does not exist: past the packing limit the projection would
        // fight itself for ever and the fluid would jitter. So the count is
        // capped at what the grid physically holds — hexagonal packing at one
        // diameter fits 2/(√3·d²) grains per unit area — and `count()` returns
        // the cap, not the wish. A coarse pitch therefore means fewer grains.
        const float cells = (float)(WORLD_W / _p.pitch) * (float)(WORLD_H / _p.pitch);
        int ceiling = (int)(0.80f * cells);
        if (ceiling > MAX_PARTS) ceiling = MAX_PARTS;
        if (ceiling < 1)         ceiling = 1;
        if (_p.particles > ceiling) _p.particles = ceiling;
        if (_p.particles < 1)       _p.particles = 1;
    }

    // A deterministic start: same seed, same film. The RNG is a plain LCG on
    // purpose — it is seeded here and nowhere else, so a test can replay a
    // frame exactly and a caller cannot accidentally depend on the host's.
    // A LATTICE, not a cloud. Seeding at random put grains on top of each other
    // from the first frame, and the non-penetration pass then blew the pile
    // apart in one step — the run opened on an explosion nobody asked for. Laid
    // out at rest spacing with a little jitter, the fluid starts already
    // satisfying its own constraint and the first thing it does is fall.
    void reset(uint32_t seed) {
        _rng = seed ? seed : 1u;
        const int n = _p.particles;
        // COUNT THE ROWS AND COLUMNS FIRST, then let the box divide itself
        // between them. Choosing a step and dividing the box by it is the
        // obvious way round and it does not fit: the column count has to be
        // floored, so the rows needed are ceil(n / floor(W / step)), and that
        // product overshoots the height by up to a row. The last row then
        // landed past the floor and the clamp below stacked all of it on
        // y = WORLD_H — a line of grains on the bottom edge, at t = 0, in the
        // one frame a user is most likely to be looking at.
        //
        // Integer counts cannot overshoot: the widest row is cols - 1 half
        // steps from the left edge and the lowest row half a step above the
        // floor, whatever n is. The rest spacing is still an upper bound on the
        // step, so a sparse fluid starts as a compact body rather than a
        // lattice stretched across the screen.
        int cols = (int)ceilf(sqrtf((float)n * (float)WORLD_W / (float)WORLD_H));
        if (cols < 1) cols = 1;
        int rows = (n + cols - 1) / cols;
        if (rows < 1) rows = 1;
        float stepX = (float)WORLD_W / (float)cols;
        float stepY = (float)WORLD_H / (float)rows;
        if (stepX > _s) stepX = _s;
        if (stepY > _s) stepY = _s;
        for (int i = 0; i < n; i++) {
            const int col = i % cols, row = i / cols;
            _x[i] = (col + 0.5f) * stepX + (rnd() - 0.5f) * 0.2f * stepX;
            _y[i] = (row + 0.5f) * stepY + (rnd() - 0.5f) * 0.2f * stepY;
            _vx[i] = _vy[i] = 0.0f;
            _px[i] = _x[i];
            _py[i] = _y[i];
        }
    }

    // gx, gy: gravity direction in screen axes, magnitude ~1 at rest (what the
    // accelerometer hands over). swirl: rotation rate, degrees per second.
    void step(float gx, float gy, float swirl, float dt) {
        _pairs = 0;                 // what the cost ceiling is measured on
        _maxPairsOne = 0;           // and what its FAIRNESS is measured on
        const int n = _p.particles;
        const float g = 900.0f * (_p.gravity / 100.0f);      // px/s^2 at 100 %
        const float sigma = 0.30f * (_p.viscosity / 100.0f); // linear term
        const float beta  = 0.08f * (_p.viscosity / 100.0f); // quadratic term
        const float w = (swirl * 3.14159265f / 180.0f) * (_p.gyroGain / 100.0f);

        // 1. gravity, and the swirl as a tangential push around the centre
        for (int i = 0; i < n; i++) {
            _vx[i] += dt * g * gx;
            _vy[i] += dt * g * gy;
            const float rx = _x[i] - WORLD_W * 0.5f;
            const float ry = _y[i] - WORLD_H * 0.5f;
            _vx[i] += dt * w * -ry;
            _vy[i] += dt * w *  rx;
        }

        buildCells(n, _h);

        // 2. viscosity: impulses along the line between approaching neighbours
        forEachPair(n, [&](int i, int j, float dx, float dy, float r) {
            const float q = r / _h;
            const float nx = dx / r, ny = dy / r;
            const float u = (_vx[i] - _vx[j]) * nx + (_vy[i] - _vy[j]) * ny;
            if (u <= 0.0f) return;
            const float I = dt * (1.0f - q) * (sigma * u + beta * u * u);
            _vx[i] -= I * nx * 0.5f;  _vy[i] -= I * ny * 0.5f;
            _vx[j] += I * nx * 0.5f;  _vy[j] += I * ny * 0.5f;
        });

        // 3. advance, previous position kept so the velocity can be re-derived
        for (int i = 0; i < n; i++) {
            clampVel(i);
            _px[i] = _x[i];  _py[i] = _y[i];
            _x[i] += dt * _vx[i];
            _y[i] += dt * _vy[i];
        }

        buildCells(n, _h);

        // 4. double density relaxation — the pressure step, without a solve.
        //
        // THE STIFFNESSES ARE IN px/s², and that is the whole point. They used
        // to be 0.5 and 2.0 against a displacement scaled by dt²·h — numbers
        // lifted from a paper whose world is a few units wide, dropped into a
        // world 320 pixels wide. The pressure they produced was worth about
        // 13 px/s² against a gravity of 900: the fluid had no volume at all and
        // fell into a one-dot line along whichever edge faced the floor. Read
        // as accelerations they can be compared with gravity, which is the only
        // comparison that decides whether a liquid holds itself up.
        const float k = PRESSURE_K, knear = PRESSURE_KNEAR, rho0 = _rho0;
        for (int i = 0; i < n; i++) { _rho[i] = 0.0f; _rhoN[i] = 0.0f; }
        forEachPair(n, [&](int i, int j, float, float, float r) {
            const float q1 = 1.0f - r / _h;
            _rho[i]  += q1 * q1;        _rho[j]  += q1 * q1;
            _rhoN[i] += q1 * q1 * q1;   _rhoN[j] += q1 * q1 * q1;
        });
        forEachPair(n, [&](int i, int j, float dx, float dy, float r) {
            const float q1 = 1.0f - r / _h;
            const float nx = dx / r, ny = dy / r;
            const float Pi = k * (_rho[i] - rho0), PiN = knear * _rhoN[i];
            const float Pj = k * (_rho[j] - rho0), PjN = knear * _rhoN[j];
            const float D = dt * dt *
                            (((Pi + Pj) * 0.5f) * q1 + ((PiN + PjN) * 0.5f) * q1 * q1);
            _x[i] -= D * nx * 0.5f;  _y[i] -= D * ny * 0.5f;
            _x[j] += D * nx * 0.5f;  _y[j] += D * ny * 0.5f;
        });

        // 5. walls: the box, and how much of the approach speed survives it.
        const float e = _p.bounce / 100.0f;
        for (int i = 0; i < n; i++) {
            if (_x[i] < 0.0f)     { _x[i] = 0.0f;     _px[i] = -e * _px[i]; }
            if (_x[i] > WORLD_W)  { _x[i] = WORLD_W;  _px[i] = WORLD_W + e * (WORLD_W - _px[i]); }
            if (_y[i] < 0.0f)     { _y[i] = 0.0f;     _py[i] = -e * _py[i]; }
            if (_y[i] > WORLD_H)  { _y[i] = WORLD_H;  _py[i] = WORLD_H + e * (WORLD_H - _py[i]); }
        }

        // 5b. NO TWO GRAINS OCCUPY THE SAME PLACE, and this pass — not the
        // pressure above — is what makes that a promise. Pressure is a restoring
        // force: it opposes compression in proportion to how compressed the
        // fluid already is, so under enough gravity it always yields a little,
        // and "a little" at 240 grains is a column of dots sitting on top of
        // each other. A separation of exactly one dot diameter is a CONSTRAINT
        // instead, so gravity has nothing to trade against it.
        //
        // IT COMES AFTER THE WALLS, and that is the difference between a
        // constraint and a suggestion. Run before them, every push it gave a
        // grain lying on the floor was immediately taken back: the projection
        // moved the grain through the floor, the wall clamped it straight back
        // into the neighbour it had just been separated from, and the pile
        // re-formed once per step for ever. Measured that way the settled fluid
        // sat at 8.9 px in a body that had promised twelve, and no amount of
        // extra passes moved it — the passes were never the shortage. Last word
        // on position, it holds; the wall is enforced inside it instead, by
        // giving a grain that cannot move the whole of its partner's push.
        buildCells(n, _d);
        separate(n);

        // 6. velocity re-derived from the actual displacement — including every
        // pixel the projection just contributed, so a grain squeezed out of a
        // pile leaves with the speed that squeezed it.
        const float inv = (dt > 0.0f) ? 1.0f / dt : 0.0f;
        for (int i = 0; i < n; i++) {
            _vx[i] = (_x[i] - _px[i]) * inv;
            _vy[i] = (_y[i] - _py[i]) * inv;
            clampVel(i);
        }
    }

    // A finger on the fluid: everything close to the point is pushed away.
    void impulse(float x, float y, float strength) {
        for (int i = 0; i < _p.particles; i++) {
            const float dx = _x[i] - x, dy = _y[i] - y;
            const float r2 = dx * dx + dy * dy;
            if (r2 < 1.0f || r2 > 70.0f * 70.0f) continue;
            const float r = sqrtf(r2);
            _vx[i] += strength * dx / r;
            _vy[i] += strength * dy / r;
            clampVel(i);
        }
    }

    // The ONLY conversion from world to grid.
    //
    // ONE GRAIN LIGHTS ONE DOT, and it is spread over the FOUR dots around it
    // in proportion to how close it sits to each — 255 of light, shared. A
    // nearest-cell drop was the obvious version and it is the wrong one twice
    // over: a grain drifting across a cell boundary jumped from one dot to the
    // next, which on a 12 px grid is a visible stutter at every crossing, and a
    // grain's brightness then said nothing about where inside the dot it was,
    // so the surface of the fluid had no soft edge — it was a staircase. With
    // the weights, motion between two dots reads as one fading while the other
    // lights, and the edge of the body dims because the outermost grains only
    // spend part of themselves there.
    //
    // Density is therefore COVERAGE, not a head count: since no two grains
    // overlap, a saturated cell means one grain sitting square on it. Speed is
    // the fastest grain touching the cell — the maximum and not the mean,
    // because one fast drop crossing a calm cell IS the thing the eye follows.
    void splat(DotGrid& out) const {
        out.cols = WORLD_W / _p.pitch;
        out.rows = WORLD_H / _p.pitch;
        const int cells = out.cols * out.rows;
        for (int i = 0; i < cells; i++) { out.dens[i] = 0; out.spd[i] = 0; }
        const float pitch = (float)_p.pitch;
        for (int i = 0; i < _p.particles; i++) {
            // Cell-centre coordinates: integer values land on a dot's middle.
            // CLAMPED to the outermost dot, because a grain resting against a
            // wall sits at the wall and half of its weight would otherwise fall
            // off the grid and be lost — which dimmed exactly the row the whole
            // fluid rests on. The last row of dots IS the floor.
            float fx = _x[i] / pitch - 0.5f, fy = _y[i] / pitch - 0.5f;
            if (fx < 0.0f) fx = 0.0f;
            if (fy < 0.0f) fy = 0.0f;
            if (fx > (float)(out.cols - 1)) fx = (float)(out.cols - 1);
            if (fy > (float)(out.rows - 1)) fy = (float)(out.rows - 1);
            const int c0 = (int)floorf(fx), r0 = (int)floorf(fy);
            const float tx = fx - (float)c0, ty = fy - (float)r0;
            const float sp = sqrtf(_vx[i] * _vx[i] + _vy[i] * _vy[i]);
            int s = (int)(sp * 255.0f / 600.0f);
            if (s > 255) s = 255;
            for (int dr = 0; dr <= 1; dr++) {
                const int r = r0 + dr;
                if (r < 0 || r >= out.rows) continue;
                const float wy = dr ? ty : 1.0f - ty;
                for (int dc = 0; dc <= 1; dc++) {
                    const int c = c0 + dc;
                    if (c < 0 || c >= out.cols) continue;
                    const float w = wy * (dc ? tx : 1.0f - tx);
                    if (w <= 0.0f) continue;
                    const int idx = r * out.cols + c;
                    const int d = out.dens[idx] + (int)(255.0f * w);
                    out.dens[idx] = (uint8_t)(d > 255 ? 255 : d);
                    if (s > out.spd[idx]) out.spd[idx] = (uint8_t)s;
                }
            }
        }
    }

    float kineticEnergy() const {
        float e = 0.0f;
        for (int i = 0; i < _p.particles; i++)
            e += _vx[i] * _vx[i] + _vy[i] * _vy[i];
        return e;
    }

    int   count() const      { return _p.particles; }
    float px(int i) const    { return _x[i]; }
    float py(int i) const    { return _y[i]; }

    // Pair evaluations in the last step: the cost, in the only unit that does
    // not depend on which machine is running it. Read by test_fluid, which
    // asserts the ceiling MAX_NEIGH is really enforced.
    uint32_t pairsLastStep() const { return _pairs; }

    // The most pairs charged to any ONE particle in the last step. The cost
    // ceiling says the total is bounded; this says the truncation is FAIR — no
    // particle is integrated against more neighbours than its neighbours were
    // allowed against it, whatever its index happens to be.
    int maxPairsForOneParticle() const { return _maxPairsOne; }

private:
    // Uniform grid of side _h: a particle only ever compares itself with the
    // nine cells around it, so the cost is linear in the particle count rather
    // than quadratic. At 400 particles the difference is 80 000 pair tests
    // saved per frame, which is the whole reason the frame fits.
    // SIZED FOR THE SMALLEST CELL THIS GRID IS EVER ASKED FOR, which is the
    // separation's — one grain diameter, itself a fraction of the finest dot
    // pitch. Sized for MIN_PITCH instead, the finest setting overflowed the
    // clamp below and folded the last two rows of cells into one bucket: every
    // grain lying on the floor landed in the same list, the separation spent
    // its whole examination bound walking it, and the fluid at pitch 8 stacked
    // where the same fluid at pitch 12 did not. A grid that silently truncates
    // is worse than one that costs a kilobyte.
    static constexpr float MIN_SIDE = GRAIN_DIAM * (float)MIN_PITCH;
    static constexpr int GC = (int)((float)WORLD_W / MIN_SIDE) + 2;
    static constexpr int GR = (int)((float)WORLD_H / MIN_SIDE) + 2;

    // THE CELL SIDE IS AN ARGUMENT, because the two things that walk this grid
    // work at different scales. The pressure looks out to _h; the separation
    // only cares about one grain diameter, and made to borrow the pressure's
    // grid it scanned nine cells covering forty-five grains to find the handful
    // that actually touched — enough to spend its whole examination bound on
    // grains that were nowhere near overlapping, and then return. That is why
    // the settled fluid still showed pairs at 0.7 of a diameter no matter how
    // many passes it was given: the passes were not the shortage.
    void buildCells(int n, float side) {
        _gc = (int)(WORLD_W / side) + 1;
        _gr = (int)(WORLD_H / side) + 1;
        if (_gc > GC) _gc = GC;
        if (_gr > GR) _gr = GR;
        for (int i = 0; i < _gc * _gr; i++) _head[i] = -1;
        for (int i = 0; i < n; i++) {
            int c = (int)(_x[i] / side), r = (int)(_y[i] / side);
            if (c < 0) c = 0;
            if (c >= _gc) c = _gc - 1;
            if (r < 0) r = 0;
            if (r >= _gr) r = _gr - 1;
            _cell[i] = r * _gc + c;
            _next[i] = _head[_cell[i]];
            _head[_cell[i]] = i;
        }
    }

    // THE CEILING IS CHARGED TO BOTH SIDES OF A PAIR, and that is not a detail.
    // Charged to the outer particle only, the cap was biased by INDEX: a pair
    // (i, j) is visited once, at i, so i pays for it and j never does. In a
    // compacted pile the low indices were truncated at MAX_NEIGH while the high
    // ones kept receiving contributions from everybody below them — measured at
    // 24 pairs for particle 0 against 59 for particle 59 in a 60-particle
    // cluster. Index is assigned once at reset() and has nothing to do with
    // position, so that turned an arbitrary number into physics: two identical
    // piles damped differently depending on who happened to be numbered first.
    // Requiring budget on BOTH sides makes the truncation symmetric.
    template <typename F>
    void forEachPair(int n, F fn) {
        for (int i = 0; i < n; i++) { _budget[i] = MAX_NEIGH; _partOf[i] = 0; }
        for (int i = 0; i < n; i++) {
            const int cr = _cell[i] / _gc, cc = _cell[i] % _gc;
            for (int dr = -1; dr <= 1 && _budget[i] > 0; dr++) {
                const int r = cr + dr;
                if (r < 0 || r >= _gr) continue;
                for (int dc = -1; dc <= 1 && _budget[i] > 0; dc++) {
                    const int c = cc + dc;
                    if (c < 0 || c >= _gc) continue;
                    for (int j = _head[r * _gc + c]; j >= 0 && _budget[i] > 0; j = _next[j]) {
                        if (j <= i) continue;            // each pair once
                        if (_budget[j] <= 0) continue;   // j is full: not its turn
                        const float dx = _x[j] - _x[i], dy = _y[j] - _y[i];
                        const float r2 = dx * dx + dy * dy;
                        if (r2 >= _h * _h || r2 < 1e-6f) continue;
                        _budget[i]--;
                        _budget[j]--;
                        // PARTICIPATION, not budget spent. Counting the budget
                        // would measure nothing: it is capped by construction,
                        // so a one-sided ceiling would still report a tidy 24
                        // while a high-index particle was quietly integrated
                        // against sixty neighbours. What has to be bounded is
                        // how many pairs a particle is IN, whichever side of
                        // them it sits on.
                        _partOf[i]++;
                        _partOf[j]++;
                        _pairs++;
                        fn(i, j, dx, dy, sqrtf(r2));
                    }
                }
            }
        }
        for (int i = 0; i < n; i++)
            if (_partOf[i] > _maxPairsOne) _maxPairsOne = _partOf[i];
    }

    // THE SEPARATION HAS ITS OWN WALK, and it needs one. Sending it through
    // forEachPair looked like reuse and quietly broke the promise twice: that
    // walk drops any pair closer than a thousandth of a pixel, so two grains
    // that had landed exactly on top of each other were the one case it refused
    // to look at — and its MAX_NEIGH budget, which exists to keep the PRESSURE
    // affordable in a collapsed pile, meant the grains most in need of pushing
    // apart were the ones whose budget had already run out. Measured: a
    // minimum gap of 0.00 px in a fluid that had just promised one dot.
    //
    // Here the test radius is one diameter instead of the interaction radius,
    // so the neighbourhood is small by construction, and the walk is bounded by
    // candidates EXAMINED rather than by pairs kept — a bound that cannot
    // discard a real overlap while a merely-nearby grain keeps its place.
    // SWEEPS, plural, and the plural is the whole difference between a promise
    // and a preference. One sweep only pushes each grain clear of the
    // neighbours it happens to see first, and in a settled pile that just moves
    // the overlap to the grain next door: measured at a single sweep, the
    // closest pair in a 240-grain fluid sat 1.3 px apart in a body that had
    // promised twelve. A projection converges when it is iterated, so it is.
    // FOUR PASSES, IN INDEX ORDER, and both halves of that were measured rather
    // than reasoned. The tempting refinement is to sweep the deepest cells
    // first, on the theory that a pile is a chain and the grain at the bottom
    // has to move before the one above it can. Measured over a settled
    // 400-grain fluid, the closest pair as a fraction of the grain diameter:
    //
    //     index order:    pitch 8  62 %   |  pitch 12  88 %   |  pitch 20  96 %
    //     deepest first:  pitch 8  72 %   |  pitch 12  75 %   |  pitch 20  96 %
    //
    // It buys the finest pitch and pays for it at the pitch most people run, so
    // it is not taken. It is also only "deepest" while gravity points down the
    // screen, and this fluid is tilted by an IMU — the ordering would silently
    // stop meaning anything the moment the robot is turned on its side, which
    // is the normal case here rather than an edge one.
    //
    // The pass count is NOT a convergence knob either, and that is the more
    // useful thing to know: at pitch 8 the same measurement gives 62 %, 51 %,
    // 69 % for 4, 8 and 16 passes. Non-monotone means the projection is
    // oscillating rather than converging, so extra passes spend frame budget
    // without buying separation. Four is where the cost stops being free.
    void separate(int n) {
        for (int pass = 0; pass < SEPARATE_PASSES; pass++)
            for (int i = 0; i < n; i++) separateOne(i);
    }

    // ONE grain against its neighbours. It is a function of its own so that
    // running out of candidates ends THIS grain's scan and not the whole pass —
    // written inline, the bound's `return` abandoned every grain after the
    // first crowded one, which is the same broken promise by another route.
    void separateOne(int i) {
        const float d2 = _d * _d;
        {
            const int cr = _cell[i] / _gc, cc = _cell[i] % _gc;
            int examined = 0;
            for (int dr = -1; dr <= 1; dr++) {
                const int r = cr + dr;
                if (r < 0 || r >= _gr) continue;
                for (int dc = -1; dc <= 1; dc++) {
                    const int c = cc + dc;
                    if (c < 0 || c >= _gc) continue;
                    for (int j = _head[r * _gc + c]; j >= 0; j = _next[j]) {
                        // EACH PAIR ONCE, from its lower index. Seeing it from
                        // both ends sounds strictly better — the push would
                        // propagate further per sweep — and it measures worse,
                        // because `examined` below is a bound on CANDIDATES and
                        // visiting both ends spends it twice as fast: the
                        // closest settled pair falls from 62 % of a diameter to
                        // 31 % at pitch 8, and from 88 % to 81 % at pitch 12.
                        // The bound is what makes the walk affordable in a
                        // collapsed pile, so what it is spent on is the whole
                        // question.
                        if (j <= i) continue;
                        if (++examined > MAX_TOUCH) return;
                        const float dx = _x[j] - _x[i], dy = _y[j] - _y[i];
                        const float r2 = dx * dx + dy * dy;
                        if (r2 >= d2) continue;
                        float nx, ny, push;
                        if (r2 < 1e-4f) {
                            // Exactly coincident: there is no direction to read
                            // off the pair, so one is CHOSEN — deterministically,
                            // because the whole bin is replayable from a seed.
                            nx = (i & 1) ? 0.0f : 1.0f;
                            ny = (i & 1) ? 1.0f : 0.0f;
                            push = 0.5f * _d;
                        } else {
                            const float rr = sqrtf(r2);
                            nx = dx / rr;  ny = dy / rr;
                            push = 0.5f * (_d - rr);
                        }
                        // Half each, unless a wall refuses one of the halves —
                        // then the other grain takes the whole overlap. A grain
                        // resting on the floor has nowhere to go, and splitting
                        // the push with it evenly means half the separation is
                        // simply lost to the clamp, once per step, exactly where
                        // the fluid is deepest.
                        const float mi = movable(i, -nx, -ny);
                        const float mj = movable(j,  nx,  ny);
                        const float total = mi + mj;
                        if (total <= 0.0f) continue;      // both pinned: nothing to do
                        const float si = 2.0f * push * mi / total;
                        const float sj = 2.0f * push * mj / total;
                        move(i, -si * nx, -si * ny);
                        move(j,  sj * nx,  sj * ny);
                    }
                }
            }
        }
    }

    // Can grain i still move along (dx, dy), or is a wall in the way? Returns
    // the share of a separation it is able to accept: 1 when free, 0 when the
    // wall it is pinned against faces exactly the way it is being pushed.
    // (dx, dy) is a unit direction; the components a wall would eat are dropped
    // and what is left is measured, so a push straight down into the floor
    // scores 0, one sliding along it keeps its horizontal part, and a grain in
    // open fluid scores 1.
    float movable(int i, float dx, float dy) const {
        float ax = dx, ay = dy;
        if (ax < 0.0f && _x[i] <= 0.0f)          ax = 0.0f;
        if (ax > 0.0f && _x[i] >= (float)WORLD_W) ax = 0.0f;
        if (ay < 0.0f && _y[i] <= 0.0f)          ay = 0.0f;
        if (ay > 0.0f && _y[i] >= (float)WORLD_H) ay = 0.0f;
        return sqrtf(ax * ax + ay * ay);
    }

    // THE CORRECTION IS DELIBERATELY LEFT IN THE VELOCITY, and that is a
    // position-based dynamics contract rather than an oversight. Step 6 reads
    // velocity off the displacement since step 3, so a push written into _x
    // becomes motion — which is exactly how a grain squeezed out of a pile
    // leaves with the speed that squeezed it, and how the pile stops being one.
    //
    // Carrying the previous position along with the correction — leaving the
    // velocity as gravity and pressure left it — is the obvious alternative and
    // it is much worse, because it removes the only mechanism that dissipates
    // an overlap. Measured over a settled 400-grain fluid, mean speed and the
    // closest pair, against the grain diameter:
    //
    //     carried:  pitch 8   49 px/s, gap  5 % of d   |  pitch 12  67 px/s,  5 %
    //     coupled:  pitch 8    7 px/s, gap 62 % of d   |  pitch 12   5 px/s, 88 %
    //
    // Both metrics move the same way, so this is not a trade: decoupling gives
    // a fluid that is simultaneously faster and more interpenetrated. The
    // projection needs the coupling.
    void move(int i, float dx, float dy) {
        _x[i] += dx;  _y[i] += dy;
        if (_x[i] < 0.0f)    _x[i] = 0.0f;
        if (_x[i] > WORLD_W) _x[i] = WORLD_W;
        if (_y[i] < 0.0f)    _y[i] = 0.0f;
        if (_y[i] > WORLD_H) _y[i] = WORLD_H;
    }

    void clampVel(int i) {
        const float s2 = _vx[i] * _vx[i] + _vy[i] * _vy[i];
        const float m = _vmax * 60.0f;                 // per second, dt = 1/60
        if (s2 > m * m) {
            const float k = m / sqrtf(s2);
            _vx[i] *= k;  _vy[i] *= k;
        }
    }

    float rnd() {
        _rng = _rng * 1664525u + 1013904223u;
        return (float)((_rng >> 8) & 0xFFFFFF) / 16777216.0f;
    }

    Params   _p;
    float    _h = 27.0f, _vmax = 13.5f;
    float    _d = 12.0f;      // grain diameter = dot pitch (the no-overlap floor)
    float    _s = 15.0f;      // rest spacing
    float    _rho0 = 1.70f;   // rest density, at that spacing and that radius
    uint32_t _rng = 1;
    float _x[MAX_PARTS] = {0},  _y[MAX_PARTS] = {0};
    float _px[MAX_PARTS] = {0}, _py[MAX_PARTS] = {0};
    float _vx[MAX_PARTS] = {0}, _vy[MAX_PARTS] = {0};
    float _rho[MAX_PARTS] = {0}, _rhoN[MAX_PARTS] = {0};
    int   _cell[MAX_PARTS] = {0}, _next[MAX_PARTS] = {0};
    int      _head[GC * GR] = {0};
    int      _gc = 1, _gr = 1;
    int      _budget[MAX_PARTS] = {0};
    int      _partOf[MAX_PARTS] = {0};
    int      _maxPairsOne = 0;
    uint32_t _pairs = 0;
};

} // namespace fluid
} // namespace sce
