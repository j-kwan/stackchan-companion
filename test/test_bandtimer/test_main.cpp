// =============================================================================
// test_bandtimer — the two interactive band modes' state machines (08-04)
// =============================================================================
// Pure module, FakeClock: the countdown, the pause freeze, the ring, the
// pomodoro cycle chain and its tuning clamps are all pinned here — the
// hardware session only has to check pixels and swipes.
// =============================================================================

#include <unity.h>
#include "engine/Clock.h"
#include "engine/Tuning.h"
#include "engine/BandTimer.h"

using namespace sce;
using K = BandTimer::Kind;
using P = BandTimer::Phase;
using E = BandTimer::Event;

extern "C" void setUp(void)    {}
extern "C" void tearDown(void) {}

// Advance `ms`, ticking every 100 ms; returns the LAST non-None event and
// counts them all — an event that fires twice is a bug the count catches.
static E run(FakeClock& clk, BandTimer& t, uint32_t ms, int* nEvents = nullptr) {
    E last = E::None;
    for (uint32_t i = 0; i < ms; i += 100) {
        clk.advanceMs(100);
        E e = t.tick(clk.ms());
        if (e != E::None) { last = e; if (nEvents) (*nEvents)++; }
    }
    return last;
}

// ---- reglage : les crans enroulent, et seulement a l'arret ------------------
// MM:SS, minutes jusqu'a 99 (forme finale 08-04).
static void test_reglage_enroule_et_fige_en_course(void) {
    FakeClock clk; Tuning tn;
    BandTimer t(tn);
    TEST_ASSERT_EQUAL_INT(5, t.setM());          // default 05:00
    TEST_ASSERT_EQUAL_INT(0, t.setS());
    t.addSeconds(-1);                            // 0 - 1 -> wraps to 59
    TEST_ASSERT_EQUAL_INT(59, t.setS());
    t.addMinutes(-6);                            // 5 - 6 -> wraps to 99
    TEST_ASSERT_EQUAL_INT(99, t.setM());
    t.addMinutes(2);                             // 99 + 2 -> 1
    TEST_ASSERT_EQUAL_INT(1, t.setM());
    t.tap(clk.ms());                             // Run
    t.addMinutes(1);                             // running: IGNORED
    TEST_ASSERT_EQUAL_INT(1, t.setM());
    // ...and PAUSED is not editable either (review 08-04). It used to be, and
    // it was incoherent: a paused countdown lives in the remainder, while the
    // setters only touch the setting, so the band did not move under the
    // finger and the change surfaced on the next arm.
    t.tap(clk.ms());                             // pause
    TEST_ASSERT_TRUE(t.phase() == P::Paused);
    const uint32_t left = t.remainingS(clk.ms());
    t.addMinutes(5);
    t.addSeconds(30);
    TEST_ASSERT_EQUAL_INT(1, t.setM());          // setting untouched
    TEST_ASSERT_EQUAL_INT(59, t.setS());         // (59 from the wrap above)
    TEST_ASSERT_EQUAL_UINT32(left, t.remainingS(clk.ms()));   // remainder too
}

// ---- minuteur : compte, sonne UNE fois, l'acquittement restaure -------------
static void test_minuteur_sonne_une_fois_et_acquitte(void) {
    FakeClock clk; Tuning tn;
    BandTimer t(tn);
    t.addMinutes(-4);                            // 01:00
    TEST_ASSERT_EQUAL_INT(1, t.setM());
    t.tap(clk.ms());
    TEST_ASSERT_TRUE(t.phase() == P::Run);
    TEST_ASSERT_EQUAL_UINT32(60, t.remainingS(clk.ms()));
    int n = 0;
    E last = run(clk, t, 61000, &n);
    TEST_ASSERT_TRUE(last == E::Ring);
    TEST_ASSERT_EQUAL_INT(1, n);                 // rings ONCE
    TEST_ASSERT_TRUE(t.phase() == P::Ring);
    run(clk, t, 5000, &n);
    TEST_ASSERT_EQUAL_INT(1, n);                 // ...and stays quiet after
    t.tap(clk.ms());                             // acknowledge
    TEST_ASSERT_TRUE(t.phase() == P::Idle);
    TEST_ASSERT_EQUAL_INT(1, t.setM());          // duration kept for reuse
}

// ---- pause : le reste est GELE, pas re-ancre sur l'horloge ------------------
static void test_pause_gele_le_reste(void) {
    FakeClock clk; Tuning tn;
    BandTimer t(tn);                        // 00:05
    t.tap(clk.ms());
    run(clk, t, 120000);                         // 2 min run -> 180 s left
    TEST_ASSERT_EQUAL_UINT32(180, t.remainingS(clk.ms()));
    t.tap(clk.ms());                             // pause
    run(clk, t, 60000);                          // a minute passes...
    TEST_ASSERT_EQUAL_UINT32(180, t.remainingS(clk.ms()));   // ...frozen
    t.tap(clk.ms());                             // resume
    run(clk, t, 60000);
    TEST_ASSERT_EQUAL_UINT32(120, t.remainingS(clk.ms()));
}

// ---- un minuteur a 00:00 ne s'arme pas --------------------------------------
static void test_zero_ne_s_arme_pas(void) {
    FakeClock clk; Tuning tn;
    BandTimer t(tn);
    t.addMinutes(-5);                            // 00:00
    TEST_ASSERT_EQUAL_INT(0, t.setM());
    t.tap(clk.ms());
    TEST_ASSERT_TRUE(t.phase() == P::Idle);
}

// ---- pomodoro : la chaine complete work/break x N puis AllDone --------------
// L'HYDRATATION EST MISE A ZERO ICI, explicitement : ce test decrit la chaine
// SANS elle, et le defaut du reglage est 1 minute. Le laisser implicite ferait
// dire au test "voici la chaine du pomodoro" alors qu'il decrirait la chaine
// d'une configuration particuliere -- et le jour ou le defaut rebouge, c'est ce
// test qui casserait au lieu de celui qui parle d'hydratation.
static void test_pomodoro_chaine_complete(void) {
    FakeClock clk; Tuning tn;
    tn.pomo_work_min = 1.0f; tn.pomo_break_min = 1.0f; tn.pomo_cycles = 2.0f;
    tn.pomo_hydra_min = 0.0f;
    BandTimer t(tn);
    t.setKind(K::Pomodoro);
    t.tap(clk.ms());
    clk.advanceMs(10);
    TEST_ASSERT_TRUE(t.tick(clk.ms()) == E::WorkStart);      // start event
    TEST_ASSERT_TRUE(t.phase() == P::Work);
    TEST_ASSERT_TRUE(run(clk, t, 61000) == E::BreakStart);   // work 1 done
    TEST_ASSERT_EQUAL_INT(1, t.cycle());
    TEST_ASSERT_TRUE(run(clk, t, 61000) == E::WorkStart);    // break 1 done
    TEST_ASSERT_EQUAL_INT(2, t.cycle());
    TEST_ASSERT_TRUE(run(clk, t, 61000) == E::AllDone);      // LAST work: no
    TEST_ASSERT_TRUE(t.phase() == P::Done);                  // trailing break
    t.tap(clk.ms());
    TEST_ASSERT_TRUE(t.phase() == P::Idle);
}

// ---- hydratation : la goutte s'intercale entre le travail et la pause -------
// L'ORDRE EST LE PROPOS. Une invite a boire posee au DEBUT de la pause est une
// invite qu'on suit en se levant ; fondue dans la pause elle n'est qu'une
// etiquette, placee apres elle coupe le retour au travail.
static void test_hydratation_s_intercale_avant_la_pause(void) {
    FakeClock clk; Tuning tn;
    tn.pomo_work_min = 1.0f; tn.pomo_break_min = 1.0f; tn.pomo_cycles = 2.0f;
    tn.pomo_hydra_min = 1.0f;
    BandTimer t(tn);
    t.setKind(K::Pomodoro);
    t.tap(clk.ms());
    clk.advanceMs(10);
    TEST_ASSERT_TRUE(t.tick(clk.ms()) == E::WorkStart);
    TEST_ASSERT_TRUE(run(clk, t, 61000) == E::HydrateStart);  // travail fini
    TEST_ASSERT_TRUE(t.phase() == P::Hydrate);
    TEST_ASSERT_TRUE(run(clk, t, 61000) == E::BreakStart);    // puis la pause
    TEST_ASSERT_TRUE(t.phase() == P::Break);
    TEST_ASSERT_TRUE(run(clk, t, 61000) == E::WorkStart);     // cycle suivant
    TEST_ASSERT_EQUAL_INT(2, t.cycle());
}

// ---- l'hydratation ne RACCOURCIT pas la pause -------------------------------
// Prendre la minute d'hydratation SUR la pause ferait payer le fait de boire :
// activer le rappel reduirait le repos. La pause qui suit est la pause reglee,
// entiere.
static void test_l_hydratation_ne_raccourcit_pas_la_pause(void) {
    FakeClock clk; Tuning tn;
    tn.pomo_work_min = 1.0f; tn.pomo_break_min = 5.0f; tn.pomo_cycles = 2.0f;
    tn.pomo_hydra_min = 1.0f;
    BandTimer t(tn);
    t.setKind(K::Pomodoro);
    t.tap(clk.ms());
    clk.advanceMs(10);
    t.tick(clk.ms());
    run(clk, t, 61000);                                       // -> Hydrate
    run(clk, t, 61000);                                       // -> Break
    TEST_ASSERT_TRUE(t.phase() == P::Break);
    // PRESQUE 300 et non 300 : `run` depasse chaque echeance d'une seconde
    // (61000 pour un bloc de 60 s), donc la pause compte deja ~2 s au moment
    // ou on la lit. Ce qui est teste est l'ordre de grandeur, et il tranche :
    // une pause raccourcie de la minute d'hydratation vaudrait 240.
    const uint32_t left = t.remainingS(clk.ms());
    TEST_ASSERT_TRUE(left > 290u && left <= 300u);
}

// ---- zero hydratation = EXACTEMENT l'ancienne machine -----------------------
// Le meme contrat que la profondeur des barres LED : "arret" ne veut pas dire
// "presque comme avant", il veut dire l'ancien comportement, exactement.
static void test_hydratation_a_zero_est_l_ancienne_chaine(void) {
    FakeClock clk; Tuning tn;
    tn.pomo_work_min = 1.0f; tn.pomo_break_min = 1.0f; tn.pomo_cycles = 2.0f;
    tn.pomo_hydra_min = 0.0f;
    BandTimer t(tn);
    t.setKind(K::Pomodoro);
    t.tap(clk.ms());
    clk.advanceMs(10);
    t.tick(clk.ms());
    TEST_ASSERT_TRUE(run(clk, t, 61000) == E::BreakStart);     // pas d'etape
    TEST_ASSERT_TRUE(t.phase() == P::Break);
}

// ---- le dernier bloc ne propose pas a boire ---------------------------------
// La session est finie : une invite a boire apres la derniere sonnerie est du
// travail apres la cloche, comme la pause que le AllDone supprime deja.
static void test_le_dernier_bloc_ne_propose_pas_a_boire(void) {
    FakeClock clk; Tuning tn;
    tn.pomo_work_min = 1.0f; tn.pomo_break_min = 1.0f; tn.pomo_cycles = 1.0f;
    tn.pomo_hydra_min = 1.0f;
    BandTimer t(tn);
    t.setKind(K::Pomodoro);
    t.tap(clk.ms());
    clk.advanceMs(10);
    t.tick(clk.ms());
    TEST_ASSERT_TRUE(run(clk, t, 61000) == E::AllDone);
    TEST_ASSERT_TRUE(t.phase() == P::Done);
}

// ---- la pause fige aussi l'hydratation --------------------------------------
static void test_la_pause_gele_l_hydratation(void) {
    FakeClock clk; Tuning tn;
    tn.pomo_work_min = 1.0f; tn.pomo_hydra_min = 2.0f; tn.pomo_cycles = 2.0f;
    BandTimer t(tn);
    t.setKind(K::Pomodoro);
    t.tap(clk.ms());
    clk.advanceMs(10);
    t.tick(clk.ms());
    run(clk, t, 61000);                                       // -> Hydrate
    run(clk, t, 60000);                                       // 1 min ecoulee
    const uint32_t left = t.remainingS(clk.ms());
    t.tap(clk.ms());                                          // pause
    TEST_ASSERT_TRUE(t.phase() == P::Paused);
    run(clk, t, 30000);
    TEST_ASSERT_EQUAL_UINT32(left, t.remainingS(clk.ms()));    // fige
    t.tap(clk.ms());                                          // reprend
    TEST_ASSERT_TRUE(t.phase() == P::Hydrate);
}

// ---- pomodoro : la pause tient au milieu d'un bloc --------------------------
static void test_pomodoro_pause_reprend_le_bloc(void) {
    FakeClock clk; Tuning tn;
    tn.pomo_work_min = 2.0f;
    BandTimer t(tn);
    t.setKind(K::Pomodoro);
    t.tap(clk.ms());
    run(clk, t, 60000);                          // 1 min into work
    t.tap(clk.ms());                             // pause
    TEST_ASSERT_TRUE(t.phase() == P::Paused);
    run(clk, t, 30000);
    t.tap(clk.ms());                             // resume -> back to Work
    TEST_ASSERT_TRUE(t.phase() == P::Work);
    TEST_ASSERT_EQUAL_UINT32(60, t.remainingS(clk.ms()));
}

// ---- changer de mode re-arme la machine -------------------------------------
static void test_changement_de_mode_rearme(void) {
    FakeClock clk; Tuning tn;
    BandTimer t(tn);
    t.tap(clk.ms());                             // timer runs (00:05)
    t.setKind(K::Pomodoro);
    TEST_ASSERT_TRUE(t.phase() == P::Idle);      // no leak across kinds
    t.setKind(K::Timer);
    TEST_ASSERT_TRUE(t.phase() == P::Idle);
}

// ---- les bornes des reglages pomodoro tiennent ------------------------------
static void test_pomodoro_bornes_tuning(void) {
    FakeClock clk; Tuning tn;
    tn.pomo_work_min = 0.0f;                     // 0 would ring forever
    tn.pomo_cycles   = 99.0f;
    BandTimer t(tn);
    t.setKind(K::Pomodoro);
    TEST_ASSERT_EQUAL_UINT32(60, t.remainingS(clk.ms()));    // clamped to 1 min
    TEST_ASSERT_EQUAL_INT(8, t.cycles());                    // clamped to 8
}

// ---- un tick dont le `now` PRECEDE le tap ne sonne pas (08-04) --------------
// loop() capture son horodatage en tete de passe ; le tap tombe au milieu
// avec un millis() plus frais. Le meme passage tickait alors avec
// now < _startMs : la soustraction non signee donnait ~49 jours ecoules,
// reste 0, sonnerie immediate a 00:00.
static void test_tick_anterieur_au_tap_ne_sonne_pas(void) {
    FakeClock clk; Tuning tn;
    BandTimer t(tn);                        // 00:05
    clk.advanceMs(5000);
    t.tap(clk.ms() + 8);                         // tap "plus frais" de 8 ms
    TEST_ASSERT_TRUE(t.tick(clk.ms()) == E::None);   // pas de Ring
    TEST_ASSERT_TRUE(t.phase() == P::Run);
    TEST_ASSERT_EQUAL_UINT32(300, t.remainingS(clk.ms()));
}

// ---- maintien = reset : retour a l'arret, reglage conserve (08-04) ----------
static void test_reset_vide_le_compte_et_garde_le_reglage(void) {
    FakeClock clk; Tuning tn;
    BandTimer t(tn);                        // 05:00
    t.addMinutes(-3);                            // 02:00
    t.tap(clk.ms());
    run(clk, t, 30000);                          // 30 s de course
    t.reset();
    TEST_ASSERT_TRUE(t.phase() == P::Idle);
    TEST_ASSERT_EQUAL_INT(2, t.setM());          // le reglage survit
    TEST_ASSERT_EQUAL_UINT32(120, t.remainingS(clk.ms()));
    // Et depuis la sonnerie aussi : le reset est l'acquittement qui presse.
    t.tap(clk.ms());
    run(clk, t, 121000);
    TEST_ASSERT_TRUE(t.phase() == P::Ring);
    t.reset();
    TEST_ASSERT_TRUE(t.phase() == P::Idle);
}

// ---- les formes du pomodoro : la table, le tour, et le cas "aucune" --------
// Le geste (swipe vertical sur le bandeau, 08-25) n'est pas teste ici -- il
// vit dans main.cpp. Ce qui est teste, c'est la seule chose qu'il consulte :
// une fonction PURE qui, a partir des trois reglages courants, dit quelle
// forme vient ensuite. Le piege qu'elle doit tenir est le troisieme cas.
static void test_les_formes_du_pomodoro_font_le_tour(void) {
    // La forme par defaut de Tuning EST la premiere ligne de la table : le
    // classique 25/5x4. Si ce n'etait pas vrai, un premier swipe sur un robot
    // sorti de sa boite sauterait une forme sans que personne ne sache
    // laquelle.
    Tuning tn;
    TEST_ASSERT_EQUAL_INT(0, BandTimer::presetIndex(tn.pomo_work_min,
                                                    tn.pomo_break_min,
                                                    tn.pomo_cycles,
                                                    tn.pomo_hydra_min));
    // Vers le haut : 0 -> 1 -> 2 -> 3 -> 0. Vers le bas : l'exact inverse --
    // c'est la propriete qui compte, un sens qui avance et l'autre qui coince
    // etant la panne classique d'un cycle ecrit en chaine de `if`.
    int i = 0;
    for (int n = 0; n < BandTimer::PRESET_N; n++) {
        const BandTimer::Preset& p = BandTimer::preset(i);
        const int up = BandTimer::nextPreset((float)p.workMin,
                                             (float)p.breakMin,
                                             (float)p.cycles,
                                             (float)p.hydraMin, +1);
        const int dn = BandTimer::nextPreset((float)p.workMin,
                                             (float)p.breakMin,
                                             (float)p.cycles,
                                             (float)p.hydraMin, -1);
        TEST_ASSERT_EQUAL_INT((i + 1) % BandTimer::PRESET_N, up);
        TEST_ASSERT_EQUAL_INT((i + BandTimer::PRESET_N - 1)
                              % BandTimer::PRESET_N, dn);
        i = up;
    }
    TEST_ASSERT_EQUAL_INT(0, i);                 // le tour est boucle
}

// Chaque forme porte son HYDRATATION (08-25) : c est le reglage que le geste
// ne touchait pas, et un curseur immobile a cote de trois qui bougent se lit
// comme une commande a moitie cablee. La valeur suit le BLOC -- plus on reste
// assis, plus le rappel vaut quelque chose -- et le sprint de 15 min la coupe.
static void test_chaque_forme_porte_son_hydratation() {
    using namespace sce;
    TEST_ASSERT_EQUAL_UINT8(1, BandTimer::preset(0).hydraMin);   // 25/5
    TEST_ASSERT_EQUAL_UINT8(2, BandTimer::preset(1).hydraMin);   // 50/10
    TEST_ASSERT_EQUAL_UINT8(3, BandTimer::preset(2).hydraMin);   // 90/20
    TEST_ASSERT_EQUAL_UINT8(0, BandTimer::preset(3).hydraMin);   // 15/3
    // Elle CROIT avec le bloc de travail sur les trois formes qui en ont une :
    // la table peut changer, cette relation est ce qu elle veut dire.
    for (int i = 0; i + 1 < 3; i++)
        TEST_ASSERT_TRUE(BandTimer::preset(i + 1).hydraMin >
                         BandTimer::preset(i).hydraMin);
}

// Le cas qui a motive `presetIndex` : des valeurs REGLEES A LA MAIN dans la
// console n'appartiennent a aucune forme. Les traiter comme la forme 0 ferait
// repondre au premier swipe par la forme 1, en sautant le classique ; pire, un
// swipe vers le bas repondrait par la derniere. La regle est : un reglage
// inconnu tombe sur la PREMIERE forme du sens demande.
static void test_un_reglage_maison_n_est_aucune_forme(void) {
    TEST_ASSERT_EQUAL_INT(-1, BandTimer::presetIndex(30.0f, 7.0f, 5.0f, 1.0f));
    TEST_ASSERT_EQUAL_INT(0, BandTimer::nextPreset(30.0f, 7.0f, 5.0f, 1.0f, +1));
    TEST_ASSERT_EQUAL_INT(BandTimer::PRESET_N - 1,
                          BandTimer::nextPreset(30.0f, 7.0f, 5.0f, 1.0f, -1));
    // And the HYDRATION alone is enough to fall out of a shape: it is one of
    // the four numbers a shape is made of, not a decoration on top (08-25).
    TEST_ASSERT_EQUAL_INT(-1, BandTimer::presetIndex(25.0f, 5.0f, 4.0f, 7.0f));
    // Et l'indice est borne des deux cotes plutot que de sortir de la table :
    // `preset()` est appelee avec ce que nextPreset a rendu, mais rien
    // n'empeche un appelant futur de se tromper.
    TEST_ASSERT_EQUAL_UINT8(BandTimer::preset(0).workMin,
                            BandTimer::preset(-3).workMin);
    TEST_ASSERT_EQUAL_UINT8(BandTimer::preset(BandTimer::PRESET_N - 1).workMin,
                            BandTimer::preset(99).workMin);
}

// La forme n'est QUE trois reglages : une fois posee dans Tuning, la machine
// s'en sert comme de n'importe quelle valeur venue du YAML ou de la console.
// C'est ce qui permet au geste de ne rien savoir de la machine.
static void test_une_forme_posee_change_la_chaine(void) {
    FakeClock clk; Tuning tn;
    tn.pomo_hydra_min = 0.0f;                    // hors sujet ici
    const BandTimer::Preset& p = BandTimer::preset(1);   // 50/10 x3
    tn.pomo_work_min  = (float)p.workMin;
    tn.pomo_break_min = (float)p.breakMin;
    tn.pomo_cycles    = (float)p.cycles;
    BandTimer t(tn);
    t.setKind(K::Pomodoro);
    TEST_ASSERT_EQUAL_INT(3, t.cycles());
    TEST_ASSERT_EQUAL_UINT32(50u * 60u, t.remainingS(clk.ms()));
    t.tap(clk.ms());
    TEST_ASSERT_TRUE(E::WorkStart == t.tick(clk.ms()));
    TEST_ASSERT_TRUE(E::BreakStart == run(clk, t, 50u * 60u * 1000u + 2000u));
}

// ---- sauter une phase : le meme aiguillage que l expiration ---------------
// skip() ne doit PAS avoir sa propre table de transitions : un bloc saute doit
// avancer le compteur de cycles, respecter la regle du dernier bloc et lever
// le meme evenement qu un bloc arrive a terme. Deux tables, ce serait deux
// reponses a « qu est-ce qui vient apres le travail ».
static void test_sauter_une_phase_suit_le_meme_chemin(void) {
    FakeClock clk; Tuning tn;
    tn.pomo_hydra_min = 0.0f;                    // chaine classique
    BandTimer t(tn);
    t.setKind(K::Pomodoro);
    t.tap(clk.ms());
    TEST_ASSERT_TRUE(E::WorkStart == t.tick(clk.ms()));
    TEST_ASSERT_TRUE(P::Work == t.phase());
    // Saute le travail : on tombe sur la pause, evenement compris.
    t.skip(clk.ms());
    TEST_ASSERT_TRUE(P::Break == t.phase());
    TEST_ASSERT_TRUE(E::BreakStart == t.tick(clk.ms()));
    // Saute la pause : bloc suivant, et le CYCLE a avance.
    t.skip(clk.ms());
    TEST_ASSERT_TRUE(P::Work == t.phase());
    TEST_ASSERT_EQUAL_INT(2, t.cycle());
    TEST_ASSERT_TRUE(E::WorkStart == t.tick(clk.ms()));
}

// L hydratation est une phase comme les autres pour skip() : on peut refuser
// de boire sans perdre la pause qui suit.
static void test_sauter_l_hydratation_laisse_la_pause(void) {
    FakeClock clk; Tuning tn;
    tn.pomo_hydra_min = 1.0f;
    BandTimer t(tn);
    t.setKind(K::Pomodoro);
    t.tap(clk.ms()); t.tick(clk.ms());
    t.skip(clk.ms());                            // travail -> hydratation
    TEST_ASSERT_TRUE(P::Hydrate == t.phase());
    t.skip(clk.ms());                            // hydratation -> pause
    TEST_ASSERT_TRUE(P::Break == t.phase());
    TEST_ASSERT_TRUE(E::BreakStart == t.tick(clk.ms()));
}

// Le DERNIER bloc saute termine la session, exactement comme s il avait
// expire : c est la regle du dernier bloc, et elle vit dans advance().
static void test_sauter_le_dernier_bloc_termine(void) {
    FakeClock clk; Tuning tn;
    tn.pomo_cycles = 1.0f; tn.pomo_hydra_min = 0.0f;
    BandTimer t(tn);
    t.setKind(K::Pomodoro);
    t.tap(clk.ms()); t.tick(clk.ms());
    t.skip(clk.ms());
    TEST_ASSERT_TRUE(P::Done == t.phase());
    TEST_ASSERT_TRUE(E::AllDone == t.tick(clk.ms()));
}

// Et skip() ne fait RIEN hors d une phase qui court : a l arret il n y a rien
// a sauter, et depuis la sonnerie c est le tap qui acquitte.
static void test_sauter_ne_fait_rien_hors_course(void) {
    FakeClock clk; Tuning tn;
    BandTimer t(tn);
    t.setKind(K::Pomodoro);
    t.skip(clk.ms());
    TEST_ASSERT_TRUE(P::Idle == t.phase());
    TEST_ASSERT_TRUE(E::None == t.tick(clk.ms()));
}

// ---- le denominateur de la barre de progression ---------------------------
// En PAUSE, le total est celui de la phase ou l on RETOURNE. Repondre le bloc
// de travail quoi qu il arrive ferait sauter d echelle une barre mise en pause
// pendant une pause, puis revenir a la reprise : l affichage se contredirait
// sur une valeur qui n a pas bouge.
static void test_total_de_phase_suit_la_pause(void) {
    FakeClock clk; Tuning tn;
    tn.pomo_work_min = 25.0f; tn.pomo_break_min = 5.0f; tn.pomo_hydra_min = 0.0f;
    BandTimer t(tn);
    t.setKind(K::Pomodoro);
    t.tap(clk.ms()); t.tick(clk.ms());
    TEST_ASSERT_EQUAL_UINT32(25u * 60u, t.phaseTotalS());
    t.skip(clk.ms());                            // -> pause
    TEST_ASSERT_EQUAL_UINT32(5u * 60u, t.phaseTotalS());
    t.tap(clk.ms());                             // pause GELEE
    TEST_ASSERT_TRUE(P::Paused == t.phase());
    TEST_ASSERT_EQUAL_UINT32(5u * 60u, t.phaseTotalS());   // et non 25 min
    // Les phases sans duree n en ont pas : la barre lit vide plutot que de
    // deviner.
    t.reset();
    TEST_ASSERT_EQUAL_UINT32(0u, t.phaseTotalS());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_reglage_enroule_et_fige_en_course);
    RUN_TEST(test_minuteur_sonne_une_fois_et_acquitte);
    RUN_TEST(test_pause_gele_le_reste);
    RUN_TEST(test_zero_ne_s_arme_pas);
    RUN_TEST(test_pomodoro_chaine_complete);
    RUN_TEST(test_hydratation_s_intercale_avant_la_pause);
    RUN_TEST(test_l_hydratation_ne_raccourcit_pas_la_pause);
    RUN_TEST(test_hydratation_a_zero_est_l_ancienne_chaine);
    RUN_TEST(test_le_dernier_bloc_ne_propose_pas_a_boire);
    RUN_TEST(test_la_pause_gele_l_hydratation);
    RUN_TEST(test_pomodoro_pause_reprend_le_bloc);
    RUN_TEST(test_changement_de_mode_rearme);
    RUN_TEST(test_pomodoro_bornes_tuning);
    RUN_TEST(test_tick_anterieur_au_tap_ne_sonne_pas);
    RUN_TEST(test_reset_vide_le_compte_et_garde_le_reglage);
    RUN_TEST(test_les_formes_du_pomodoro_font_le_tour);
    RUN_TEST(test_chaque_forme_porte_son_hydratation);
    RUN_TEST(test_un_reglage_maison_n_est_aucune_forme);
    RUN_TEST(test_une_forme_posee_change_la_chaine);
    RUN_TEST(test_sauter_une_phase_suit_le_meme_chemin);
    RUN_TEST(test_sauter_l_hydratation_laisse_la_pause);
    RUN_TEST(test_sauter_le_dernier_bloc_termine);
    RUN_TEST(test_sauter_ne_fait_rien_hors_course);
    RUN_TEST(test_total_de_phase_suit_la_pause);
    UNITY_END();
    return 0;
}
