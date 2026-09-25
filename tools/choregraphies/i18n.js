/* Choreography editor — bilingual strings (EN / FR)
 * ---------------------------------------------------------------------------
 * English is the CANONICAL language: it is what sits in `index.html` as plain
 * text, so the page stays readable even if this file fails to load or if
 * JavaScript is disabled. French is applied ON TOP, at runtime.
 *
 * The robot has its own switch — a `lang` key in `config.yaml` that the
 * console, the API and the guest binaries all read. The editor cannot use it:
 * it runs on a PC, from `file://`, with no access to the SD card. Hence a
 * button of its own, and a choice remembered in `localStorage`.
 *
 * HOW THE MARKUP IS WIRED — three attributes, no framework:
 *     data-i18n="key"        replaces textContent
 *     data-i18n-html="key"   replaces innerHTML (for text carrying <b>/<code>)
 *     data-i18n-title="key"  replaces the `title` attribute (tooltips)
 * Anything built by JavaScript goes through `t(key, vars)` instead, with
 * `{name}` placeholders — never string concatenation, or half a sentence ends
 * up untranslatable.
 *
 * ADDING A STRING: add the key to BOTH tables. `missingKeys()` reports any
 * key present in one and absent from the other — a silent gap would show an
 * English sentence in the middle of a French interface without a word.
 */
'use strict';

window.I18N = {

  // ── English — canonical ───────────────────────────────────────────────
  en: {
    lv_h:        'Capture from the robot',
    lv_grab:     '⤓ Capture into the keyframe',
    lv_follow:   'follow live',
    lv_idle:     'Off. Turning it on RELEASES the servos: the head goes limp and you place it by hand.',
    lv_conn:     'Connecting…',
    lv_live:     'Live. Pose the head, then capture — or tick “follow live” to write every movement into the selected keyframe.',
    lv_wait:     'Servos released, waiting for the first measurement…',
    lv_noff:     'The robot refuses to release the servos (servos=1). Check the Options tab of its console.',
    lv_noip:     'Enter the robot’s IP address first.',
    lv_neterr:   'The robot did not answer. Wrong address, or it is off.',
    lv_blocked:  'The browser blocked the request. Turn CORS on in the robot’s console (System · Cross-origin access) and RESTART it — the header is read at boot.',
    doc_title:   'Choreographies — StackChan',
    h1_html:     'Choreographies <b>StackChan</b>',

    import:      'Import CSV…',
    import_t:    'Load a dance from a CSV file — replaces the current sequence',
    download:    'Download CSV',
    download_t:  'Save the sequence as .csv, to drop into /dances/ on the SD card',
    lang_t:      'Switch the interface to French',

    tl_h:        'Timeline',
    add_t:       'Append a keyframe at the end of the sequence',
    play:        '▶ Play',
    stop:        '■ Stop',
    play_t:      'Play the sequence at real durations — dragging the head interrupts it',
    newSeq:      'New sequence',
    newSeq_t:    'Start from an empty sequence — the current one is LOST',

    kf_h:        'Keyframe',
    f_expr:      'Expression',
    f_yaw:       'Yaw — head',
    f_pitch:     'Pitch — head',
    f_gaze:      'Vertical gaze',
    f_lids:      'Eyelids',
    f_servo:     'Servo travel time',
    f_hold:      'Total keyframe duration',
    lid_none:    'none',
    lid_blink:   'blink',
    lid_winkL:   'wink left',
    lid_winkR:   'wink right',
    rhythm_html: 'Timing references: snappy move 80-150 ms · normal gesture ' +
                 '250-400 ms · slow slide 500-700 ms · expressive hold ' +
                 '≥ 600 ms beyond the travel.',

    prev_h:      'Preview',
    scene_t:     'Drag to orient the head',
    r_yaw:       'yaw',
    r_pitch:     'pitch',
    r_gaze:      'gaze',
    endstop:     'end stop',
    drag_html:   '<b>Drag on the head</b> to set the servos: horizontally the ' +
                 '<b>yaw</b>, vertically the <b>pitch</b>. The sliders and the ' +
                 'CSV follow. Travel downwards is short — <code>pitch</code> ' +
                 'only goes to <b>+6°</b> — and “end stop” lights up as soon ' +
                 'as you reach it: that is the real servo’s limit, not the ' +
                 'tool’s.',
    fidelity_html:
      'The eyes replay the firmware’s chain: preset <b>per eye</b> ' +
      '(12 emotions are asymmetric), the flips of <code>EyeRig::mirrored</code>, ' +
      'the invariants of <code>normalize</code>, <code>EyeDrawer</code> tracing ' +
      'corner radius by corner radius, and the <b>real color</b> of the ' +
      'expression (<code>emotionToRgb</code>, dimmed by ' +
      '<code>eye_color_dim</code>). The special renders are in there too: the ' +
      'star of <code>Excited</code>, the cross of <code>Dead</code>, the ' +
      'blushing cheeks, the sparkles and the sweat drop. The data is extracted ' +
      'from the sources, nothing is copied by hand.<br>' +
      'What is <b>not</b> reproduced: idle animations (breathing, saccades, ' +
      'micro-overshoot), VOR, dynamic preset variants (Sad turning into Scary ' +
      'when looking up) and the CRT effect. The band below the eyes is only a ' +
      'framing reference: a reminder that the screen is 240 px tall and the ' +
      'eyes take up just 160 of them. The tool shows the <b>pose</b> of a ' +
      'keyframe, not the life of the robot around it.',

    csv_h_html:  'CSV — <code>/dances/&lt;name&gt;.csv</code> on the SD card',
    csv_hint_html:
      'Editable directly: the timeline updates on every valid keystroke. The ' +
      'file name gives the dance its name. Columns: ' +
      '<code>yaw,pitch,servoMs,holdMs,emotion,lid,gazeY</code>.',

    // ---- built by JavaScript ----
    unchanged_opt: '(unchanged)',
    unchanged_v:   'unchanged ({emo})',
    total:         '{n} keyframe{s} · {sec} s',
    dup_t:         'Duplicate keyframe {n} right after it',
    del_t:         'Delete keyframe {n}',
    up_t:          'Swap keyframe {n} with the previous one',
    down_t:        'Swap keyframe {n} with the next one',

    w_hold:        'hold {hold} ms < servo {servo} ms: the robot will hold {servo} ms.',
    w_gaze:        'gaze saturates at ±{max}: beyond that, no further effect.',
    w_max:         '{n} keyframes: the robot keeps only {max}.',
    w_last_normal: 'last keyframe: it should return to Normal.',
    w_last_empty:  'last keyframe: emotion column EMPTY — the robot wants an ' +
                   'explicit Normal, otherwise it appends an exit keyframe.',
    w_last_pose:   'last keyframe: the pose should be neutral (yaw 0, pitch 0).',

    err_presets_html: 'presets.js is incomplete — run ' +
                      '<code>python tools/choregraphies/extract-presets.py</code>.',
    prompt_name:   'Dance name (without .csv)',
    default_name:  'my-dance',
    alert_name:    'Name empty after cleanup (letters, digits, - and _).',
    alert_nokf:    'No readable keyframe in this file.',
    confirm_del:     'Delete keyframe {n} ({emo}, {ms} ms)?',
    confirm_newseq:  'Discard the current sequence ({n} keyframes) and start '
                     + 'from an empty one?',
    confirm_import:  'Import {file}? The current sequence ({n} keyframes) is '
                     + 'REPLACED and cannot be recovered.',

    confirm_trunc: '{n} keyframes: the robot will keep only {max}. Continue?',
  },

  // ── Français ──────────────────────────────────────────────────────────
  fr: {
    lv_h:        'Capture depuis le robot',
    lv_grab:     '⤓ Capturer dans la keyframe',
    lv_follow:   'suivre en direct',
    lv_idle:     'Éteint. L’activer RELÂCHE les servos : la tête devient molle et vous la posez à la main.',
    lv_conn:     'Connexion…',
    lv_live:     'En direct. Posez la tête puis capturez — ou cochez « suivre en direct » pour écrire chaque mouvement dans la keyframe sélectionnée.',
    lv_wait:     'Servos relâchés, en attente de la première mesure…',
    lv_noff:     'Le robot refuse de relâcher les servos (servos=1). Vérifiez l’onglet Options de sa console.',
    lv_noip:     'Saisissez d’abord l’adresse IP du robot.',
    lv_neterr:   'Le robot n’a pas répondu. Mauvaise adresse, ou il est éteint.',
    lv_blocked:  'Le navigateur a bloqué la requête. Activez CORS dans la console du robot (Système · Accès cross-origin) et REDÉMARREZ-le — l’en-tête est lu au boot.',
    doc_title:   'Chorégraphies — StackChan',
    h1_html:     'Chorégraphies <b>StackChan</b>',

    import:      'Importer CSV…',
    import_t:    'Charger une danse depuis un fichier CSV — remplace la séquence en cours',
    download:    'Télécharger le CSV',
    download_t:  'Enregistrer la séquence en .csv à déposer dans /dances/ de la carte SD',
    lang_t:      'Passer l’interface en anglais',

    tl_h:        'Timeline',
    add_t:       'Ajouter une keyframe à la fin de la séquence',
    play:        '▶ Lire',
    stop:        '■ Arrêter',
    play_t:      'Jouer la séquence aux durées réelles — un glissé sur la tête l’interrompt',
    newSeq:      'Nouvelle séquence',
    newSeq_t:    'Repartir d’une séquence vide — la séquence en cours est PERDUE',

    kf_h:        'Keyframe',
    f_expr:      'Expression',
    f_yaw:       'Yaw — tête',
    f_pitch:     'Pitch — tête',
    f_gaze:      'Regard vertical',
    f_lids:      'Paupières',
    f_servo:     'Durée du trajet servo',
    f_hold:      'Durée totale de la keyframe',
    lid_none:    'aucun',
    lid_blink:   'clignement',
    lid_winkL:   'clin d’œil gauche',
    lid_winkR:   'clin d’œil droit',
    rhythm_html: 'Repères de rythme : mouvement sec 80-150 ms · geste normal ' +
                 '250-400 ms · glisse lente 500-700 ms · tenue expressive ' +
                 '≥ 600 ms au-delà du trajet.',

    prev_h:      'Aperçu',
    scene_t:     'Glisser pour orienter la tête',
    r_yaw:       'yaw',
    r_pitch:     'pitch',
    r_gaze:      'regard',
    endstop:     'butée',
    drag_html:   '<b>Glissez sur la tête</b> pour régler les servos : ' +
                 'horizontalement le <b>yaw</b>, verticalement le <b>pitch</b>. ' +
                 'Les curseurs et le CSV suivent. La course descendante est ' +
                 'courte — <code>pitch</code> ne va que jusqu’à <b>+6°</b> — ' +
                 'et « butée » s’allume dès qu’on l’atteint : c’est la ' +
                 'limite du servo réel, pas celle de l’outil.',
    fidelity_html:
      'Les yeux rejouent la chaîne du firmware : preset <b>par œil</b> ' +
      '(12 émotions sont asymétriques), inversions de ' +
      '<code>EyeRig::mirrored</code>, invariants de <code>normalize</code>, ' +
      'tracé de <code>EyeDrawer</code> rayon par coin, et la <b>couleur ' +
      'réelle</b> de l’expression (<code>emotionToRgb</code>, assombrie par ' +
      '<code>eye_color_dim</code>). Les rendus spéciaux y sont aussi : ' +
      'l’étoile d’<code>Excited</code>, la croix de <code>Dead</code>, les ' +
      'joues rosies, les étincelles et la goutte de sueur. Les données sont ' +
      'extraites des sources, rien n’est recopié.<br>' +
      'Ce qui n’est <b>pas</b> reproduit : animations d’inactivité ' +
      '(respiration, saccades, micro-overshoot), VOR, variantes dynamiques de ' +
      'preset (Sad qui devient Scary en regardant le ciel) et effet CRT. Le ' +
      'bandeau sous les yeux n’est qu’un repère de cadrage : il rappelle ' +
      'que l’écran fait 240 px de haut et que les yeux n’en occupent que ' +
      '160. L’outil montre la <b>pose</b> d’une keyframe, pas la vie du ' +
      'robot autour.',

    csv_h_html:  'CSV — <code>/dances/&lt;nom&gt;.csv</code> sur la carte SD',
    csv_hint_html:
      'Modifiable directement : la timeline se met à jour à chaque frappe ' +
      'valide. Le nom du fichier donne le nom de la danse. Colonnes : ' +
      '<code>yaw,pitch,servoMs,holdMs,emotion,lid,gazeY</code>.',

    // ---- built by JavaScript ----
    unchanged_opt: '(inchangée)',
    unchanged_v:   'inchangée ({emo})',
    total:         '{n} keyframe{s} · {sec} s',
    dup_t:         'Dupliquer la keyframe {n} juste après',
    del_t:         'Supprimer la keyframe {n}',
    up_t:          'Échanger la keyframe {n} avec la précédente',
    down_t:        'Échanger la keyframe {n} avec la suivante',

    w_hold:        'hold {hold} ms < servo {servo} ms : le robot tiendra {servo} ms.',
    w_gaze:        'le regard sature à ±{max} : au-delà, aucun effet supplémentaire.',
    w_max:         '{n} keyframes : le robot n’en garde que {max}.',
    w_last_normal: 'dernière keyframe : elle devrait revenir à Normal.',
    w_last_empty:  'dernière keyframe : colonne émotion VIDE — le robot veut un ' +
                   'Normal explicite, sinon il ajoute une keyframe de sortie.',
    w_last_pose:   'dernière keyframe : la pose devrait être neutre (yaw 0, pitch 0).',

    err_presets_html: 'presets.js incomplet — lancez ' +
                      '<code>python tools/choregraphies/extract-presets.py</code>.',
    prompt_name:   'Nom de la danse (sans .csv)',
    default_name:  'ma-danse',
    alert_name:    'Nom vide après nettoyage (lettres, chiffres, - et _).',
    alert_nokf:    'Aucune keyframe lisible dans ce fichier.',
    confirm_del:     'Supprimer la keyframe {n} ({emo}, {ms} ms) ?',
    confirm_newseq:  'Abandonner la séquence en cours ({n} keyframes) et '
                     + 'repartir d’une séquence vide ?',
    confirm_import:  'Importer {file} ? La séquence en cours ({n} keyframes) '
                     + 'est REMPLACÉE, sans retour possible.',

    confirm_trunc: '{n} keyframes : le robot n’en gardera que {max}. Continuer ?',
  },
};

// ── Machinery ───────────────────────────────────────────────────────────
// Chosen language: an explicit choice already made (localStorage), otherwise
// ENGLISH. Deliberately NOT the browser's language — the robot follows the same
// rule (no `lang` key in `config.yaml` means English), and one predictable
// default everywhere beats two clever ones that disagree.
// `localStorage` can throw on `file://` under a strict policy — the tool must
// keep working without persistence, so every access is guarded.
window.LANGS = ['en', 'fr'];

function langStore(read, value) {
  try {
    if (read) return localStorage.getItem('sce-lang');
    localStorage.setItem('sce-lang', value);
  } catch (_) { /* file:// without storage: the choice lasts one session */ }
  return null;
}

window.pickLang = function () {
  const saved = langStore(true);
  return (saved && window.LANGS.includes(saved)) ? saved : 'en';
};

window.lang = window.pickLang();

// Translate `key`, substituting {placeholders}. An unknown key returns the key
// itself rather than an empty string: a visible anomaly gets fixed, a blank
// label goes unnoticed.
window.t = function (key, vars) {
  const table = window.I18N[window.lang] || window.I18N.en;
  let s = table[key];
  if (s === undefined) s = window.I18N.en[key];
  if (s === undefined) return key;
  if (vars) {
    for (const k of Object.keys(vars)) s = s.split('{' + k + '}').join(vars[k]);
  }
  return s;
};

// Applies the current language to the whole document.
window.applyLang = function () {
  document.documentElement.lang = window.lang;
  document.title = window.t('doc_title');
  for (const el of document.querySelectorAll('[data-i18n]'))
    el.textContent = window.t(el.getAttribute('data-i18n'));
  for (const el of document.querySelectorAll('[data-i18n-html]'))
    el.innerHTML = window.t(el.getAttribute('data-i18n-html'));
  for (const el of document.querySelectorAll('[data-i18n-title]'))
    el.title = window.t(el.getAttribute('data-i18n-title'));
};

window.setLang = function (l) {
  if (!window.LANGS.includes(l)) return;
  window.lang = l;
  langStore(false, l);
  window.applyLang();
};

// Reports any key present in one table and missing from the other. Called at
// startup: a gap between the two languages otherwise shows up as an English
// sentence inside a French interface, with nothing to explain it.
window.missingKeys = function () {
  const out = [];
  for (const a of window.LANGS) {
    for (const b of window.LANGS) {
      if (a === b) continue;
      for (const k of Object.keys(window.I18N[a]))
        if (!(k in window.I18N[b])) out.push(b + ' is missing "' + k + '"');
    }
  }
  return out;
};
