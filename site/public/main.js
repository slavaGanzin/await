// ── Copy install ─────────────────────────────────────────────────────────
function copyInstall() {
  const cmd = document.getElementById('install-cmd').textContent;
  navigator.clipboard.writeText(cmd).then(() => {
    const btn = document.querySelector('.copy-btn');
    btn.classList.add('copied');
    setTimeout(() => btn.classList.remove('copied'), 2000);
  });
}

// ── WebGL background: color blobs + aurora lines + dot grid ──────────────
(function initWebGL() {
  const canvas = document.getElementById('bgCanvas');
  const gl = canvas.getContext('webgl') || canvas.getContext('experimental-webgl');
  if (!gl) return;

  const VERT = `
    attribute vec2 a_pos;
    void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }
  `;

  const FRAG = `
    precision mediump float;
    uniform vec2  u_res;
    uniform float u_time;
    uniform float u_slide;

    vec3 primary(float i) {
      float m = mod(i, 13.0);
      if (m < 0.5)  return vec3(0.08, 0.10, 0.92);
      if (m < 1.5)  return vec3(0.60, 0.02, 0.16);
      if (m < 2.5)  return vec3(0.00, 0.54, 0.84);
      if (m < 3.5)  return vec3(0.18, 0.04, 0.90);
      if (m < 4.5)  return vec3(0.50, 0.04, 0.92);
      if (m < 5.5)  return vec3(0.04, 0.20, 0.94);
      if (m < 6.5)  return vec3(0.00, 0.58, 0.70);
      if (m < 7.5)  return vec3(0.86, 0.52, 0.00);
      if (m < 8.5)  return vec3(0.14, 0.04, 0.80);
      if (m < 9.5)  return vec3(0.00, 0.42, 0.90);
      if (m < 10.5) return vec3(0.72, 0.00, 0.52);
      if (m < 11.5) return vec3(0.32, 0.04, 0.86);
      return         vec3(0.08, 0.10, 0.90);
    }

    vec3 secondary(float i) {
      float m = mod(i, 13.0);
      if (m < 0.5)  return vec3(0.38, 0.04, 0.72);
      if (m < 1.5)  return vec3(0.22, 0.00, 0.52);
      if (m < 2.5)  return vec3(0.00, 0.28, 0.56);
      if (m < 3.5)  return vec3(0.42, 0.00, 0.66);
      if (m < 4.5)  return vec3(0.16, 0.00, 0.72);
      if (m < 5.5)  return vec3(0.00, 0.08, 0.72);
      if (m < 6.5)  return vec3(0.00, 0.32, 0.50);
      if (m < 7.5)  return vec3(0.46, 0.26, 0.00);
      if (m < 8.5)  return vec3(0.10, 0.00, 0.62);
      if (m < 9.5)  return vec3(0.00, 0.18, 0.66);
      if (m < 10.5) return vec3(0.32, 0.00, 0.62);
      if (m < 11.5) return vec3(0.16, 0.00, 0.66);
      return         vec3(0.24, 0.04, 0.66);
    }

    void main() {
      vec2 uv = gl_FragCoord.xy / u_res.xy;
      uv.y = 1.0 - uv.y;
      float aspect = u_res.x / u_res.y;

      float t = u_time * 0.05;
      float slideOff = u_slide * 0.55;

      float si    = floor(u_slide);
      float blend = smoothstep(0.0, 1.0, fract(u_slide));
      vec3 pc = mix(primary(si),   primary(si   + 1.0), blend);
      vec3 sc = mix(secondary(si), secondary(si + 1.0), blend);
      vec3 mc = mix(pc, sc, 0.5);

      vec3 col = vec3(0.005, 0.006, 0.018);

      // Color blobs
      vec2 b1 = vec2(0.14 + sin(t * 0.80) * 0.10, 0.20 + cos(t * 0.62) * 0.08);
      vec2 d1 = uv - b1; d1.x *= aspect;
      col += pc * exp(-dot(d1, d1) * 2.0) * 0.42;

      vec2 b2 = vec2(0.82 + cos(t * 0.72) * 0.08, 0.78 + sin(t * 0.88) * 0.08);
      vec2 d2 = uv - b2; d2.x *= aspect;
      col += sc * exp(-dot(d2, d2) * 1.9) * 0.40;

      vec2 b3 = vec2(0.50 + sin(t * 0.50) * 0.13, 0.47 + cos(t * 0.44) * 0.11);
      vec2 d3 = uv - b3; d3.x *= aspect;
      col += mc * exp(-dot(d3, d3) * 3.5) * 0.28;

      vec2 b4 = vec2(0.84 + sin(t * 1.10) * 0.06, 0.12 + cos(t * 0.90) * 0.06);
      vec2 d4 = uv - b4; d4.x *= aspect;
      col += sc * exp(-dot(d4, d4) * 6.0) * 0.22;

      vec2 b5 = vec2(0.12 + cos(t * 0.94) * 0.07, 0.83 + sin(t * 0.76) * 0.07);
      vec2 d5 = uv - b5; d5.x *= aspect;
      col += pc * exp(-dot(d5, d5) * 6.5) * 0.20;

      // Aurora lines: crisp neon core + wide soft glow
      float lw  = 2.0  / u_res.y;
      float glw = 45.0 / u_res.y;
      float ya; float la; float ga;

      ya = sin(uv.x * 2.8 * aspect + t * 0.55 + slideOff       ) * 0.11 + 0.38;
      la = smoothstep(lw, 0.0, abs(uv.y - ya));
      ga = smoothstep(glw, 0.0, abs(uv.y - ya));
      col += pc * (la * 0.90 + ga * 0.11);

      ya = sin(uv.x * 2.1 * aspect + t * 0.40 + slideOff + 1.20) * 0.09 + 0.62;
      la = smoothstep(lw, 0.0, abs(uv.y - ya));
      ga = smoothstep(glw, 0.0, abs(uv.y - ya));
      col += sc * (la * 0.85 + ga * 0.10);

      ya = sin(uv.x * 3.4 * aspect + t * 0.75 + slideOff + 2.40) * 0.07 + 0.28;
      la = smoothstep(lw, 0.0, abs(uv.y - ya));
      ga = smoothstep(glw, 0.0, abs(uv.y - ya));
      col += mc * (la * 0.72 + ga * 0.08);

      ya = sin(uv.x * 1.7 * aspect + t * 0.32 + slideOff + 3.60) * 0.13 + 0.74;
      la = smoothstep(lw, 0.0, abs(uv.y - ya));
      ga = smoothstep(glw, 0.0, abs(uv.y - ya));
      col += sc * (la * 0.68 + ga * 0.07);

      ya = sin(uv.x * 4.0 * aspect + t * 0.90 + slideOff + 5.00) * 0.05 + 0.52;
      la = smoothstep(lw, 0.0, abs(uv.y - ya));
      ga = smoothstep(glw, 0.0, abs(uv.y - ya));
      col += pc * (la * 0.58 + ga * 0.06);

      ya = sin(uv.x * 1.4 * aspect + t * 0.22 + slideOff + 0.80) * 0.15 + 0.16;
      la = smoothstep(lw, 0.0, abs(uv.y - ya));
      ga = smoothstep(glw, 0.0, abs(uv.y - ya));
      col += mc * (la * 0.62 + ga * 0.06);

      // Dot grid — micro-texture so backdrop-filter has something to blur
      float cellPx = 52.0;
      vec2 dotUV = fract(gl_FragCoord.xy / cellPx
                         + vec2(sin(t * 0.12) * 1.5, cos(t * 0.10) * 1.5)) - 0.5;
      float dotMask = smoothstep(0.22, 0.10, length(dotUV));
      col += dotMask * 0.06 * mc;

      col = col / (col + 0.25);
      col = pow(col, vec3(0.88));

      gl_FragColor = vec4(col, 1.0);
    }
  `;

  function compileShader(type, src) {
    const s = gl.createShader(type);
    gl.shaderSource(s, src);
    gl.compileShader(s);
    return s;
  }

  const prog = gl.createProgram();
  gl.attachShader(prog, compileShader(gl.VERTEX_SHADER, VERT));
  gl.attachShader(prog, compileShader(gl.FRAGMENT_SHADER, FRAG));
  gl.linkProgram(prog);
  gl.useProgram(prog);

  const buf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, buf);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1,-1, 1,-1, -1,1, 1,1]), gl.STATIC_DRAW);
  const loc = gl.getAttribLocation(prog, 'a_pos');
  gl.enableVertexAttribArray(loc);
  gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);

  const uRes   = gl.getUniformLocation(prog, 'u_res');
  const uTime  = gl.getUniformLocation(prog, 'u_time');
  const uSlide = gl.getUniformLocation(prog, 'u_slide');

  let slideTarget = 0;
  let slideSmooth = 0;

  function resize() {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    canvas.width  = window.innerWidth  * dpr;
    canvas.height = window.innerHeight * dpr;
    gl.viewport(0, 0, canvas.width, canvas.height);
  }
  resize();
  window.addEventListener('resize', resize);

  function render(ts) {
    const secs = ts / 1000;
    slideSmooth += (slideTarget - slideSmooth) * 0.055;
    gl.uniform2f(uRes, canvas.width, canvas.height);
    gl.uniform1f(uTime, secs);
    gl.uniform1f(uSlide, slideSmooth);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    requestAnimationFrame(render);
  }
  requestAnimationFrame(render);

  window._setShaderSlide = (idx) => { slideTarget = idx; };
})();

// ── Slide navigation + html-ppt animation pattern ────────────────────────
(function initSlideNav() {
  const slides   = Array.from(document.querySelectorAll('.slide'));
  const dotsEl   = document.getElementById('slideDots');
  const slidesEl = document.getElementById('slides');
  const progressEl = document.getElementById('slideProgress');
  const counterEl  = document.getElementById('slideCounter');

  const dots = slides.map((slide, i) => {
    const btn = document.createElement('button');
    btn.className = 'slide-dot';
    btn.setAttribute('aria-label', `Slide ${i + 1}`);
    btn.addEventListener('click', () => goTo(i));
    dotsEl.appendChild(btn);
    return btn;
  });

  // html-ppt pattern: cascade [data-anim] elements in with stagger delay.
  // Once animated, elements stay visible (anim-done is permanent).
  function animIn(slide) {
    slide.querySelectorAll('[data-anim]').forEach((el, i) => {
      if (el.classList.contains('anim-done')) return;
      setTimeout(() => el.classList.add('anim-done'), i * 115);
    });
  }

  function goTo(i) {
    const h = slides[0].getBoundingClientRect().height;
    slidesEl.scrollTop = i * h;
  }

  function setActive(i) {
    slides.forEach((s, j) => s.classList.toggle('is-active', j === i));
    dots.forEach((d, j) => d.classList.toggle('active', j === i));
    animIn(slides[i]);
    if (window._setShaderSlide) window._setShaderSlide(i);

    // Progress bar (html-ppt deck-footer pattern)
    if (progressEl) {
      const pct = slides.length > 1 ? (i / (slides.length - 1)) * 100 : 100;
      progressEl.style.width = `${pct}%`;
    }

    // Slide counter
    if (counterEl) counterEl.textContent = `${i + 1} / ${slides.length}`;
  }

  // IntersectionObserver: fire setActive when slide is ≥50% visible
  const io = new IntersectionObserver(entries => {
    entries.forEach(e => {
      if (e.isIntersecting && e.intersectionRatio >= 0.5) {
        setActive(slides.indexOf(e.target));
      }
    });
  }, { threshold: 0.5 });
  slides.forEach(s => io.observe(s));

  // Keyboard: arrows, space, page keys
  document.addEventListener('keydown', e => {
    const cur = dots.findIndex(d => d.classList.contains('active'));
    if (e.key === 'ArrowDown' || e.key === 'PageDown' || e.key === ' ') {
      e.preventDefault();
      if (cur < slides.length - 1) goTo(cur + 1);
    } else if (e.key === 'ArrowUp' || e.key === 'PageUp') {
      e.preventDefault();
      if (cur > 0) goTo(cur - 1);
    } else if (e.key === 'Home') {
      e.preventDefault();
      goTo(0);
    } else if (e.key === 'End') {
      e.preventDefault();
      goTo(slides.length - 1);
    }
  });

  // Init first slide
  setActive(0);
})();
