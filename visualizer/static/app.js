import * as THREE from "three";
import { OrbitControls } from "three/addons/OrbitControls.js";

const WATER = "#3c6e8f";
const CHARRED = "#21272b";
const FIRE_RAMP = ["#862f2b", "#dd4a2c", "#f7822e", "#ffc757", "#fff6b7"];
const FIRE_STOPS = [0, 0.14, 0.38, 0.67, 1];
const WIND_COLOR = "#6be0f0";
const MAX_FLAMES = 4000;

const $ = (id) => document.getElementById(id);
const rgb = (hex) => [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16));
const unit = (hex) => new THREE.Vector3(...rgb(hex).map((v) => v / 255));

const canvas = $("view");
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
renderer.setPixelRatio(window.devicePixelRatio);
const scene = new THREE.Scene();
scene.background = new THREE.Color("#101d2a");
const camera = new THREE.PerspectiveCamera(35, 1, 0.1, 50000);
const controls = new OrbitControls(camera, canvas);
controls.enableDamping = true;
controls.maxPolarAngle = Math.PI * 0.48;

const state = { run: null, time: 0, playing: false, exaggeration: 1 };
const clock = new THREE.Clock();

const terrainVertex = `
  uniform float uMetersPerUnit;
  varying vec2 vUv;
  varying vec3 vNormal;
  varying float vElevation;
  void main() {
    vUv = uv;
    vNormal = normal;
    vElevation = position.y * uMetersPerUnit;
    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
  }
`;

const terrainFragment = `
  uniform sampler2D uColor;
  uniform sampler2D uFire;
  uniform float uTime;
  uniform float uContour;
  uniform vec3 uRamp[5];
  uniform vec3 uCharred;
  varying vec2 vUv;
  varying vec3 vNormal;
  varying float vElevation;

  vec3 ramp(float x) {
    x = clamp(x, 0.0, 1.0);
    if (x < 0.14) return mix(uRamp[0], uRamp[1], x / 0.14);
    if (x < 0.38) return mix(uRamp[1], uRamp[2], (x - 0.14) / 0.24);
    if (x < 0.67) return mix(uRamp[2], uRamp[3], (x - 0.38) / 0.29);
    return mix(uRamp[3], uRamp[4], (x - 0.67) / 0.33);
  }

  float contourLine(float h) {
    float fw = fwidth(h);
    if (fw < 0.002) return 0.0;
    return 1.0 - clamp(abs(fract(h - 0.5) - 0.5) / fw, 0.0, 1.0);
  }

  void main() {
    vec3 base = texture2D(uColor, vUv).rgb;
    vec4 fire = texture2D(uFire, vUv);
    float light = 0.62 + 0.5 * max(dot(normalize(vNormal), normalize(vec3(0.15, 1.0, 0.5))), 0.0);
    vec3 color = base * light;

    if (fire.r >= 0.0 && uTime >= fire.r) {
      float phase = (uTime - fire.r) / max(fire.g, 1.0);
      if (phase < 1.0) {
        float tri = 1.0 - abs(2.0 * phase - 1.0);
        float strength = fire.b >= 0.0 ? fire.b * (0.55 + 0.45 * tri) : tri;
        color = ramp(strength) * (0.85 + 0.15 * light);
      } else {
        color = uCharred * light;
      }
    }

    if (uContour > 0.0) {
      float h = vElevation / uContour;
      color = mix(color, vec3(0.66, 0.79, 0.73), 0.3 * contourLine(h));
      color = mix(color, vec3(0.91, 0.96, 0.83), 0.6 * contourLine(h / 5.0));
    }
    gl_FragColor = vec4(color, 1.0);
  }
`;

function niceInterval(range) {
  const raw = range / 12;
  const step = 10 ** Math.floor(Math.log10(raw));
  return [1, 2, 2.5, 5, 10].map((m) => m * step).find((v) => v >= raw);
}

function makeTexture(data, cols, rows, type) {
  const texture = new THREE.DataTexture(data, cols, rows, THREE.RGBAFormat, type);
  texture.magFilter = THREE.NearestFilter;
  texture.minFilter = THREE.NearestFilter;
  texture.needsUpdate = true;
  return texture;
}

function fireColor(strength, target) {
  const x = Math.min(Math.max(strength, 0), 1);
  let i = 0;
  while (i < FIRE_STOPS.length - 2 && x > FIRE_STOPS[i + 1]) i++;
  const f = (x - FIRE_STOPS[i]) / (FIRE_STOPS[i + 1] - FIRE_STOPS[i]);
  return target.lerpColors(new THREE.Color(FIRE_RAMP[i]), new THREE.Color(FIRE_RAMP[i + 1]), f);
}

function percentile(values, p) {
  const sorted = values.filter((v) => v !== null && v > 0).sort((a, b) => a - b);
  return sorted.length ? sorted[Math.floor((sorted.length - 1) * p)] : 0;
}

function buildRun(data) {
  const { rows, cols, arrays } = data;
  const n = rows * cols;
  const elevation = Float32Array.from(arrays.elevation_m);
  const ignition = Float32Array.from(arrays.ignition_time_s);
  const duration = Float32Array.from(arrays.burn_duration_s);
  const intensity = arrays.intensity_kw_m ?? null;
  const intensityMax = intensity ? percentile(intensity, 0.99) : 0;

  const geometry = new THREE.BufferGeometry();
  const uv = new Float32Array(n * 2);
  for (let i = 0; i < rows; i++) {
    for (let j = 0; j < cols; j++) {
      uv[(i * cols + j) * 2] = (j + 0.5) / cols;
      uv[(i * cols + j) * 2 + 1] = (i + 0.5) / rows;
    }
  }
  const index = [];
  for (let i = 0; i < rows - 1; i++) {
    for (let j = 0; j < cols - 1; j++) {
      const a = i * cols + j;
      index.push(a, a + cols, a + 1, a + 1, a + cols, a + cols + 1);
    }
  }
  const position = new THREE.BufferAttribute(new Float32Array(n * 3), 3);
  geometry.setAttribute("position", position);
  geometry.setAttribute("uv", new THREE.BufferAttribute(uv, 2));
  geometry.setIndex(index);

  const colors = new Uint8Array(n * 4);
  const typeColors = Object.fromEntries(
    Object.entries(data.terrain_types).map(([id, t]) => [id, rgb(t.color)]),
  );
  const water = rgb(WATER);
  for (let k = 0; k < n; k++) {
    const c = arrays.water_mask?.[k] ? water : typeColors[arrays.terrain_type[k]];
    colors.set([...c, 255], k * 4);
  }

  const fire = new Float32Array(n * 4);
  let endTime = 0;
  for (let k = 0; k < n; k++) {
    const v = intensity?.[k];
    const level = v == null || intensityMax === 0 ? -1 : Math.min(Math.log1p(v) / Math.log1p(intensityMax), 1);
    fire.set([ignition[k], duration[k], level, 0], k * 4);
    if (ignition[k] >= 0) endTime = Math.max(endTime, ignition[k] + duration[k]);
  }

  let low = Infinity;
  let high = -Infinity;
  for (const v of elevation) {
    low = Math.min(low, v);
    high = Math.max(high, v);
  }
  const contour = high - low > 1 ? niceInterval(high - low) : 0;

  const material = new THREE.ShaderMaterial({
    vertexShader: terrainVertex,
    fragmentShader: terrainFragment,
    polygonOffset: true,
    polygonOffsetFactor: 1,
    polygonOffsetUnits: 1,
    uniforms: {
      uColor: { value: makeTexture(colors, cols, rows, THREE.UnsignedByteType) },
      uFire: { value: makeTexture(fire, cols, rows, THREE.FloatType) },
      uTime: { value: 0 },
      uContour: { value: contour },
      uMetersPerUnit: { value: 1 },
      uRamp: { value: FIRE_RAMP.map(unit) },
      uCharred: { value: unit(CHARRED) },
    },
  });
  const terrain = new THREE.Mesh(geometry, material);

  const edge = [];
  for (let j = 0; j < cols; j++) edge.push(j);
  for (let i = 1; i < rows; i++) edge.push(i * cols + cols - 1);
  for (let j = cols - 2; j >= 0; j--) edge.push((rows - 1) * cols + j);
  for (let i = rows - 2; i > 0; i--) edge.push(i * cols);
  const outlineGeometry = new THREE.BufferGeometry();
  outlineGeometry.setAttribute("position", position);
  outlineGeometry.setIndex(edge);
  const outline = new THREE.LineLoop(
    outlineGeometry,
    new THREE.LineBasicMaterial({ color: "#70a39c", transparent: true, opacity: 0.5 }),
  );

  const cone = new THREE.ConeGeometry(0.3, 1, 6).translate(0, 0.5, 0);
  const flames = new THREE.InstancedMesh(
    cone,
    new THREE.MeshBasicMaterial({ transparent: true, opacity: 0.94 }),
    MAX_FLAMES,
  );
  flames.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
  flames.count = 0;

  const wind = new THREE.Mesh(
    new THREE.BufferGeometry(),
    new THREE.MeshBasicMaterial({
      color: WIND_COLOR,
      transparent: true,
      opacity: 0.85,
      side: THREE.DoubleSide,
      depthWrite: false,
    }),
  );

  scene.add(terrain, outline, flames, wind);
  return {
    data, rows, cols, elevation, ignition, duration, fire, terrain, outline, flames, wind,
    endTime, contour, intensityMax, hasIntensity: intensityMax > 0,
  };
}

function heightAt(run, r, c) {
  const i0 = Math.min(Math.max(Math.floor(r), 0), run.rows - 1);
  const j0 = Math.min(Math.max(Math.floor(c), 0), run.cols - 1);
  const i1 = Math.min(i0 + 1, run.rows - 1);
  const j1 = Math.min(j0 + 1, run.cols - 1);
  const fr = Math.min(Math.max(r - i0, 0), 1);
  const fc = Math.min(Math.max(c - j0, 0), 1);
  const e = (i, j) => run.elevation[i * run.cols + j];
  const top = e(i0, j0) * (1 - fc) + e(i0, j1) * fc;
  const bottom = e(i1, j0) * (1 - fc) + e(i1, j1) * fc;
  return top * (1 - fr) + bottom * fr;
}

function buildWind(run) {
  const { rows, cols, data } = run;
  const speed = data.arrays.wind_speed_kmh;
  const from = data.arrays.wind_from_deg;
  const size = Math.max(rows, cols);
  const spacing = Math.max(3, Math.round(size / 7));
  const lift = size * 0.05;
  const scale = state.exaggeration / data.cell_size_m;
  const vertices = [];
  let total = 0;
  let arrows = 0;

  const point = (r, c) => [c - cols / 2, heightAt(run, r, c) * scale + lift, r - rows / 2];

  for (let r0 = spacing / 2; r0 < rows; r0 += spacing) {
    for (let c0 = spacing / 2; c0 < cols; c0 += spacing) {
      let east = 0;
      let north = 0;
      let count = 0;
      for (let i = Math.floor(r0 - spacing / 2); i < Math.min(rows, r0 + spacing / 2); i++) {
        for (let j = Math.floor(c0 - spacing / 2); j < Math.min(cols, c0 + spacing / 2); j++) {
          if (i < 0 || j < 0) continue;
          const k = i * cols + j;
          const a = (from[k] * Math.PI) / 180;
          east -= Math.sin(a) * speed[k];
          north -= Math.cos(a) * speed[k];
          count++;
        }
      }
      east /= count;
      north /= count;
      const kmh = Math.hypot(east, north);
      if (kmh < 1) continue;
      total += kmh;
      arrows++;

      const dc = east / kmh;
      const dr = -north / kmh;
      const length = spacing * Math.min(0.7, 0.2 + kmh / 60);
      const half = spacing * 0.012;
      const head = spacing * 0.12;
      const headHalf = spacing * 0.06;
      const steps = 6;
      const shaft = [];
      for (let s = 0; s <= steps; s++) {
        const f = -length / 2 + ((length - head) * s) / steps;
        shaft.push([r0 + dr * f, c0 + dc * f]);
      }
      for (let s = 0; s < steps; s++) {
        const [ra, ca] = shaft[s];
        const [rb, cb] = shaft[s + 1];
        const al = point(ra - dc * half, ca + dr * half);
        const ar = point(ra + dc * half, ca - dr * half);
        const bl = point(rb - dc * half, cb + dr * half);
        const br = point(rb + dc * half, cb - dr * half);
        vertices.push(...al, ...ar, ...bl, ...bl, ...ar, ...br);
      }
      const [re, ce] = shaft[steps];
      const tip = point(re + dr * head, ce + dc * head);
      const left = point(re - dc * headHalf, ce + dr * headHalf);
      const right = point(re + dc * headHalf, ce - dr * headHalf);
      vertices.push(...left, ...right, ...tip);
    }
  }

  run.wind.geometry.dispose();
  run.wind.geometry = new THREE.BufferGeometry();
  run.wind.geometry.setAttribute("position", new THREE.Float32BufferAttribute(vertices, 3));
  $("wind-info").textContent = arrows
    ? `Sparse regional vectors · mean ${(total / arrows).toFixed(0)} km/h`
    : "No wind";
}

function applyExaggeration() {
  const run = state.run;
  const scale = state.exaggeration / run.data.cell_size_m;
  const position = run.terrain.geometry.attributes.position;
  for (let i = 0; i < run.rows; i++) {
    for (let j = 0; j < run.cols; j++) {
      const k = i * run.cols + j;
      position.setXYZ(k, j - run.cols / 2, run.elevation[k] * scale, i - run.rows / 2);
    }
  }
  position.needsUpdate = true;
  run.terrain.geometry.computeVertexNormals();
  run.terrain.geometry.computeBoundingSphere();
  run.outline.geometry.computeBoundingSphere();
  run.terrain.material.uniforms.uMetersPerUnit.value = 1 / scale;
  buildWind(run);
  $("exaggeration-value").textContent = `${state.exaggeration}×`;
}

const matrix = new THREE.Matrix4();
const flameColor = new THREE.Color();

function updateFire(seconds) {
  const run = state.run;
  const burning = [];
  let reached = 0;
  for (let k = 0; k < run.ignition.length; k++) {
    const t0 = run.ignition[k];
    if (t0 < 0 || state.time < t0) continue;
    reached++;
    if (state.time < t0 + run.duration[k]) burning.push(k);
  }
  $("burning").textContent = burning.length.toLocaleString();
  $("reached").textContent = `${reached.toLocaleString()} / ${run.ignition.length.toLocaleString()}`;

  const step = Math.max(1, Math.ceil(burning.length / MAX_FLAMES));
  const scale = state.exaggeration / run.data.cell_size_m;
  let count = 0;
  for (let b = 0; b < burning.length; b += step) {
    const k = burning[b];
    const phase = (state.time - run.ignition[k]) / Math.max(run.duration[k], 1);
    const tri = 1 - Math.abs(2 * phase - 1);
    const level = run.fire[k * 4 + 2];
    const strength = level >= 0 ? level * (0.55 + 0.45 * tri) : tri;
    const flicker = 0.8 + 0.25 * Math.sin(seconds * 10 + k * 1.7);
    const i = Math.floor(k / run.cols);
    const j = k % run.cols;
    matrix.makeScale(0.5 + 0.5 * strength, (0.6 + 1.6 * strength) * flicker, 0.5 + 0.5 * strength);
    matrix.setPosition(j - run.cols / 2, run.elevation[k] * scale, i - run.rows / 2);
    run.flames.setMatrixAt(count, matrix);
    run.flames.setColorAt(count, fireColor(0.25 + 0.75 * strength, flameColor));
    count++;
  }
  run.flames.count = count;
  run.flames.instanceMatrix.needsUpdate = true;
  if (run.flames.instanceColor) run.flames.instanceColor.needsUpdate = true;
}

function formatTime(s) {
  const m = Math.floor(s / 60);
  return `${String(m).padStart(2, "0")}:${String(Math.floor(s % 60)).padStart(2, "0")}`;
}

function setTime(t) {
  const run = state.run;
  state.time = Math.min(Math.max(t, 0), run.endTime);
  run.terrain.material.uniforms.uTime.value = state.time;
  $("time").value = state.time;
  $("time").style.setProperty("--progress", `${(100 * state.time) / Math.max(run.endTime, 1)}%`);
  $("clock").textContent = `T + ${formatTime(state.time)}`;
}

function setPlaying(playing) {
  state.playing = playing;
  $("play").textContent = playing ? "Pause" : "Play";
}

function resetView() {
  const size = Math.max(state.run.rows, state.run.cols);
  camera.position.set(size * 0.65, size * 1.15, size * 1.3);
  controls.target.set(0, 0, 0);
  controls.update();
}

function showRunInfo(data, run) {
  const mock = data.model.toLowerCase().includes("mock");
  const name = data.run.split("/").pop().replaceAll("_", " ").toUpperCase();
  $("scenario").textContent = `${mock ? "SYNTHETIC TEST SCENARIO" : "PROPAGATOR RUN"}  /  ${name}`;
  $("footer-info").textContent =
    (mock ? "MOCK SPREAD, NOT PROPAGATOR  ·  " : "") +
    `${data.cell_size_m} m GRID  ·  ` +
    (run.contour ? `CONTOURS ${run.contour} m  ·  ` : "") +
    "WIND IN km/h";
  $("end-time").textContent = formatTime(run.endTime);

  $("ramp").style.background = `linear-gradient(to top, ${FIRE_RAMP.map(
    (c, i) => `${c} ${FIRE_STOPS[i] * 100}%`,
  ).join(", ")})`;
  if (run.hasIntensity) {
    $("intensity-unit").textContent = "kW / m";
    $("high-label").textContent = "VERY HIGH";
    $("low-label").textContent = "LOW";
    $("intensity-max").textContent = Math.round(run.intensityMax).toLocaleString();
    $("intensity-min").textContent = "0";
  } else {
    $("intensity-unit").textContent = "Not provided; colour follows burn phase";
    $("high-label").textContent = "MID-BURN";
    $("low-label").textContent = "IGNITION / BURNOUT";
    $("intensity-max").textContent = "";
    $("intensity-min").textContent = "";
  }

  const entries = [
    ...Object.values(data.terrain_types).map((t) => [t.color, t.name]),
    [WATER, "Water"],
  ];
  $("legend").innerHTML = entries
    .map(([c, name]) => `<div><span class="swatch" style="background:${c}"></span>${name}</div>`)
    .join("");
}

async function loadRun(id, startTime = 0) {
  const data = await (await fetch(`/api/runs/${id}`)).json();
  if (state.run) {
    const old = state.run;
    scene.remove(old.terrain, old.outline, old.flames, old.wind);
    old.terrain.geometry.dispose();
    old.terrain.material.dispose();
    old.outline.geometry.dispose();
    old.flames.dispose();
    old.wind.geometry.dispose();
  }
  state.run = buildRun(data);
  $("time").max = state.run.endTime;
  showRunInfo(data, state.run);
  applyExaggeration();
  resetView();
  setPlaying(false);
  setTime(startTime);
}

function resize() {
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  renderer.setSize(width, height, false);
  camera.aspect = width / height;
  camera.updateProjectionMatrix();
}

function animate() {
  const dt = clock.getDelta();
  if (state.run) {
    if (state.playing) {
      setTime(state.time + dt * Number($("speed").value));
      if (state.time >= state.run.endTime) setPlaying(false);
    }
    updateFire(clock.elapsedTime);
  }
  controls.update();
  renderer.render(scene, camera);
  requestAnimationFrame(animate);
}

$("run").addEventListener("change", (e) => loadRun(e.target.value));
$("time").addEventListener("input", (e) => setTime(Number(e.target.value)));
$("play").addEventListener("click", () => {
  if (state.time >= state.run.endTime) setTime(0);
  setPlaying(!state.playing);
});
$("exaggeration").addEventListener("input", (e) => {
  state.exaggeration = Number(e.target.value);
  applyExaggeration();
});
$("reset-view").addEventListener("click", resetView);
window.addEventListener("resize", resize);

const params = new URLSearchParams(window.location.search);
const runs = await (await fetch("/api/runs")).json();
$("run").innerHTML = runs.map((r) => `<option value="${r}">${r}</option>`).join("");
const initial = runs.includes(params.get("run")) ? params.get("run") : runs[0];
$("run").value = initial;
resize();
await loadRun(initial, Number(params.get("t") ?? 0));
animate();
