// evigit - the launcher's map / bus / duty pickers.
//
// One fetch, three lists, one choice each. The state is deliberately tiny: what is picked
// from each step, and what the server last told us. Everything else is derived, so the
// page cannot show a duty list that belongs to a bus the user has since changed.
//
// No framework and no build step: the file is served from disk exactly as written here.

'use strict';

const state = {
  maps: [],
  vehicles: [],
  timetables: [],
  problems: [],
  selected: { map: null, bus: null, duty: null },
  busFilter: '',
};

const $ = (id) => document.getElementById(id);

const el = {
  root: $('root'),
  counts: $('counts'),
  refresh: $('refresh'),
  maps: $('maps'),
  mapDetail: $('map-detail'),
  buses: $('buses'),
  busDetail: $('bus-detail'),
  busFilter: $('bus-filter'),
  duties: $('duties'),
  dutyDetail: $('duty-detail'),
  start: $('start'),
  chosen: $('chosen'),
  overlay: $('overlay'),
  overlayTitle: $('overlay-title'),
  overlayText: $('overlay-text'),
  overlayDetail: $('overlay-detail'),
};

// ---------------------------------------------------------------- helpers

// Nothing here builds HTML from server text: text always goes through textContent.
function node(tag, className, text) {
  const n = document.createElement(tag);
  if (className) n.className = className;
  if (text !== undefined) n.textContent = text;
  return n;
}

function showProblem(title, text, detail) {
  el.overlayTitle.textContent = title;
  el.overlayText.textContent = text;
  if (detail) {
    el.overlayDetail.textContent = detail;
    el.overlayDetail.hidden = false;
  } else {
    el.overlayDetail.hidden = true;
  }
  el.overlay.hidden = false;
}

/** Fills a list, remembering which id is selected. */
function renderList(target, items, selectedId, onPick, tagOf, emptyText) {
  target.replaceChildren();

  if (items.length === 0) {
    const li = node('li', 'empty', emptyText);
    li.setAttribute('role', 'presentation');
    target.appendChild(li);
    return;
  }

  for (const item of items) {
    const li = node('li');
    li.setAttribute('role', 'option');
    li.setAttribute('aria-selected', item.id === selectedId ? 'true' : 'false');
    li.tabIndex = 0;

    li.appendChild(node('span', 'name', item.label));

    const tag = tagOf ? tagOf(item) : '';
    if (tag) li.appendChild(node('span', 'tag', tag));

    const pick = () => onPick(item);
    li.addEventListener('click', pick);
    li.addEventListener('keydown', (event) => {
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        pick();
      } else if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
        event.preventDefault();
        const siblings = [...target.querySelectorAll('li[role="option"]')];
        const at = siblings.indexOf(li);
        const next = siblings[at + (event.key === 'ArrowDown' ? 1 : -1)];
        if (next) next.focus();
      }
    });

    target.appendChild(li);
  }
}

/** Keeps the header badge in step with the choice. */
function markStep(stepId, done) {
  const section = document.getElementById(stepId);
  if (section) section.classList.toggle('done', done);
}

function updateSummary() {
  const { map, bus, duty } = state.selected;
  const values = { map: map && map.label, bus: bus && bus.label, duty: duty && duty.label };
  for (const slot of el.chosen.querySelectorAll('.slot')) {
    const value = values[slot.dataset.step];
    slot.querySelector('i').textContent = value || '—';
    slot.classList.toggle('set', Boolean(value));
  }
  // A duty is optional: the player may want to drive a free roam instead.
  el.start.disabled = !(map && bus);
}

/** A definition list of key/value pairs. */
function facts(rows) {
  const list = node('dl', 'facts');
  for (const [key, value] of rows) {
// ------------------------------------------------------------------ render

function renderCounts() {
  el.counts.replaceChildren();
  for (const [label, value] of [
    ['maps', state.maps.length],
    ['buses', state.vehicles.length],
    ['duties', state.timetables.length],
  ]) {
    const span = node('span');
    span.appendChild(node('b', null, String(value)));
    span.append(' ' + label);
    el.counts.appendChild(span);
  }
}

function renderMaps() {
  const items = state.maps.map((m) => ({ ...m, label: m.title || m.id }));
  renderList(el.maps, items, state.selected.map && state.selected.map.id,
    selectMap, null, 'No map folder with a global.cfg was found.');

  const map = state.selected.map;
  if (!map) {
    el.mapDetail.replaceChildren(node('p', 'placeholder', 'Choose a map.'));
    return;
  }
  const detail = document.createDocumentFragment();
  detail.append(node('h3', null, map.title || map.id));
  detail.append(node('p', 'sub', 'Map folder'));
  detail.append(facts([['Folder', map.folder], ['File', map.globalCfg]]));
  detail.append(node('p', 'hint',
    'The launcher reads this folder; the game reads its global.cfg, its tiles and its terrain.'));
  el.mapDetail.replaceChildren(detail);
}

function renderBuses() {
  const needle = state.busFilter.trim().toLowerCase();
  const items = state.vehicles
    .filter((v) => !needle
      || v.name.toLowerCase().includes(needle)
      || v.manufacturer.toLowerCase().includes(needle)
      || v.model.toLowerCase().includes(needle))
    .map((v) => ({ ...v, label: v.name }));

  renderList(el.buses, items, state.selected.bus && state.selected.bus.id,
    selectBus, (item) => item.kind,
    needle ? 'Nothing matches that filter.' : 'No vehicle definition was found.');

  const bus = state.selected.bus;
  if (!bus) {
    el.busDetail.replaceChildren(node('p', 'placeholder', 'Choose a bus.'));
    return;
  }
  const detail = document.createDocumentFragment();
  detail.append(node('h3', null, bus.name));
  detail.append(node('p', 'sub', bus.manufacturer || 'Uncategorised'));
  detail.append(facts([['Kind', bus.kind], ['File', bus.file]]));
/**
 * The duty list is filtered by the chosen bus's manufacturer. A stock installation has
 * one timetable per city per operator, and showing all of them at once is how a picker
 * turns into a list nobody scrolls.
 */
function dutiesForBus() {
  const bus = state.selected.bus;
  if (!bus) return [];

  // Timetables belong to a map, so a duty only makes sense on the map that was chosen.
  // Falling back to a name match keeps the list useful when no map is picked.
  const map = state.selected.map;
  const mapId = (map && (map.id || map.title) || '').toLowerCase();
  const maker = (bus.manufacturer || '').toLowerCase();
  const model = (bus.model || '').toLowerCase();

  return state.timetables
    .filter((t) => {
      const tm = (t.map || '').toLowerCase();
      if (mapId && tm && tm !== mapId) return false;
      if (maker && tm && tm.includes(maker)) return true;
      // A "MAN_SD202" manufacturer and a "Berlin_AI" timetable rarely share a name, so
      // fall back to one name containing the other.
      const tn = (t.model || '').toLowerCase();
      if (model && tn && (tn.includes(model) || model.includes(tn))) return true;
      // A map-wide duty belongs to every vehicle that runs on that map.
      return !model && !maker;
    })
    .map((t) => ({ ...t, label: t.name }));
}

function renderDuties() {
  const items = dutiesForBus();
  const bus = state.selected.bus;

  renderList(el.duties, items, state.selected.duty && state.selected.duty.id,
    selectDuty, (item) => item.kind,
    !bus ? 'Choose a bus first.' : 'No timetable belongs to this bus.');

  const duty = state.selected.duty;
  if (!duty) {
    el.dutyDetail.replaceChildren(node('p', 'placeholder',
      bus ? 'Choose a duty, or start without one.' : 'Choose a bus first.'));
    return;
  }
  const detail = document.createDocumentFragment();
  detail.append(node('h3', null, duty.name));
  detail.append(node('p', 'sub', duty.manufacturer || 'Uncategorised'));
  detail.append(facts([['Kind', duty.kind], ['File', duty.file]]));
  el.dutyDetail.replaceChildren(detail);
}

// ----------------------------------------------------------------- picking

// Each pick cascades: changing an earlier step invalidates the ones after it, because a
// duty chosen for a different bus is not the duty the player meant.
function selectMap(map) {
  state.selected.map = map;
  state.selected.bus = null;
  state.selected.duty = null;
  markStep('step-map', true);
  renderMaps();
  renderBuses();
  renderDuties();
  updateSummary();
}

function selectBus(bus) {
  state.selected.bus = bus;
  state.selected.duty = null;
  markStep('step-bus', true);
  renderBuses();
  renderDuties();
  updateSummary();
}
// -------------------------------------------------------------------- load

function apply(payload) {
  state.maps = payload.maps || [];
  state.vehicles = payload.vehicles || [];
  state.timetables = payload.timetables || [];
  state.problems = payload.problems || [];

  el.root.textContent = payload.root || '';
  el.root.title = payload.root || '';

  // A choice that is no longer in the list must not survive the reload.
  const stillThere = (list, item) => item && list.some((x) => x.id === item.id);
  if (!stillThere(state.maps, state.selected.map)) state.selected.map = null;
  if (!stillThere(state.vehicles, state.selected.bus)) state.selected.bus = null;
  if (!stillThere(state.timetables, state.selected.duty)) state.selected.duty = null;

  markStep('step-map', Boolean(state.selected.map));
  markStep('step-bus', Boolean(state.selected.bus));
  markStep('step-duty', Boolean(state.selected.duty));

  renderCounts();
  renderMaps();
  renderBuses();
  renderDuties();
  updateSummary();

  // A half-installed mod leaves files unreadable. That is worth saying, but it is not a
  // reason to hide the picker.
  if (state.problems.length > 0) {
    showProblem(
      `${state.problems.length} file(s) could not be read`,
      'The rest of the installation was read normally. The details are below.',
      state.problems.join('\n'));
  }
}

async function load() {
  try {
    const response = await fetch('api/library', { cache: 'no-store' });
    if (!response.ok) {
      throw new Error(`the server answered ${response.status} ${response.statusText}`);
    }
    apply(await response.json());
  } catch (error) {
    showProblem(
      'Could not read the installation',
      'The launcher server did not answer. Is evigit still running?',
      String(error && error.message ? error.message : error));
  }
}

// ------------------------------------------------------------------- wiring

el.refresh.addEventListener('click', load);

el.busFilter.addEventListener('input', () => {
  state.busFilter = el.busFilter.value;
  renderBuses();
});

el.start.addEventListener('click', () => {
  const { map, bus, duty } = state.selected;
  if (!map || !bus) return;
  // Starting the game is the next piece of work. Rather than opening a dialogue that goes
  // nowhere, show what would be run.
  const parts = [`map=${map.id}`, `bus=${bus.id}`];
  if (duty) parts.push(`duty=${duty.id}`);
  showProblem('Not implemented yet',
    'Picking a session and starting the game comes next. This is what would be run:',
    `openomsi --root <install> ${parts.join(' ')}`);
});

load();

function selectDuty(duty) {
  state.selected.duty = duty;
  markStep('step-duty', true);
  renderDuties();
  updateSummary();
}
  el.busDetail.replaceChildren(detail);
}
    list.append(node('dt', null, key));
    list.append(node('dd', 'path', value));
  }
  return list;
}