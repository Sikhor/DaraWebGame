const { animate } = anime;

function playerHitEffect() {
  const card =
    (window.playerDomEntry && window.playerDomEntry.card) ||
    document.querySelector(".playerAbs .mobCard");

  const wrap =
    (window.playerDomEntry && window.playerDomEntry.wrap) ||
    document.querySelector(".playerAbs");

  if (!card || !wrap || !window.anime) return;

  // Extract current HP scale from wrapper
  const m = (wrap.style.transform || "").match(/scale\(([^)]+)\)/);
  const scale = m ? Math.max(0.2, Number(m[1]) || 1) : 1;

  const amp = Math.round(18 / scale); // visually consistent shake

  window.anime.remove(card);

  // 1️⃣ Shake (position only)
  window.anime.animate(card, {
    translateX: [
      0,
      -amp,
      amp,
      -Math.round(amp * 0.7),
      Math.round(amp * 0.7),
      0
    ],
    duration: 260,
    easing: "easeOutQuad"
  });

  // 2️⃣ Small red glow pulse
  window.anime.animate(card, {
    boxShadow: [
      "0 0 0px rgba(255,0,0,0)",
      "0 0 24px rgba(255,40,40,0.85)",
      "0 0 0px rgba(255,0,0,0)"
    ],
    duration: 320,
    easing: "easeOutQuad"
  });

  // 3️⃣ Very subtle brightness flash
  window.anime.animate(card, {
    filter: [
      "brightness(1) saturate(1)",
      "brightness(1.8) saturate(1.6)",
      "brightness(1) saturate(1)"
    ],
    duration: 180,
    easing: "linear"
  });
}




function playerHealEffect() {
  const card =
    (window.playerDomEntry && window.playerDomEntry.card) ||
    document.querySelector(".playerAbs .mobCard");

  if (!card || !window.anime) return;

  window.anime.remove(card);

  window.anime.animate(card, {
    scale: [1, 1.07, 1],
    duration: 320,
    easing: "easeOutQuad"
  });

  window.anime.animate(card, {
    opacity: [1, 0.7, 1],
    duration: 220,
    easing: "linear"
  });
}


function playerHitExtremely() {
  const wrap =
    (window.playerDomEntry && window.playerDomEntry.wrap) ||
    document.querySelector(".playerAbs");
  const card =
    (window.playerDomEntry && window.playerDomEntry.card) ||
    document.querySelector(".playerAbs .mobCard");

  if (!wrap || !card || !window.anime) {
    console.warn("[playerHitExtremely] missing wrap/card/anime", { wrap: !!wrap, card: !!card, anime: !!window.anime });
    return;
  }

  // 🔥 Make sure you SEE it even if scaled small
  window.anime.remove([wrap, card]);

  // big body shake (also helps when scaled)
  document.body.classList.add("hitShake");
  setTimeout(() => document.body.classList.remove("hitShake"), 450);

  // if you have your #hitFlash element
  const flash = document.getElementById("hitFlash");
  if (flash) {
    flash.classList.remove("on");
    void flash.offsetHeight;
    flash.style.opacity = "0.65";
    flash.classList.add("on");
  }

  // 1) violent shake of wrapper (position)
  window.anime.animate(wrap, {
    translateX: [0, -40, 40, -32, 32, -24, 24, -16, 16, 0],
    translateY: [0,  10, -10,  8, -8,  6, -6,  4, -4, 0],
    duration: 520,
    easing: "easeOutQuad"
  });

  // 2) crazy impact on the card (visual)
  window.anime.animate(card, {
    rotate: [0, -25, 25, -18, 18, -10, 10, 0],
    scale:  [1, 1.35, 0.92, 1.25, 0.98, 1.12, 1],
    duration: 520,
    easing: "easeOutElastic(1, .5)"
  });

  // 3) red/white flash (filter) + opacity blink
  window.anime.animate(card, {
    filter: [
      "brightness(1) saturate(1)",
      "brightness(2.6) saturate(3) contrast(1.4)",
      "brightness(1) saturate(1)"
    ],
    opacity: [1, 0.55, 1, 0.7, 1],
    duration: 420,
    easing: "linear"
  });

  // 4) huge temporary glow (boxShadow)
  window.anime.animate(card, {
    boxShadow: [
      "0 0 0px rgba(255,0,0,0)",
      "0 0 60px rgba(255,40,40,0.95)",
      "0 0 0px rgba(255,0,0,0)"
    ],
    duration: 520,
    easing: "easeOutQuad"
  });
}


function playerHealEffectVisible() {
  const card =
    (window.playerDomEntry && window.playerDomEntry.card) ||
    document.querySelector(".playerAbs .mobCard");

  if (!card || !window.anime) return;

  window.anime.remove(card);

  // glow pulse + brightness pulse (no transform conflicts)
  window.anime.animate(card, {
    boxShadow: [
      "0 0 0px rgba(0,255,120,0)",
      "0 0 45px rgba(0,255,120,0.95)",
      "0 0 0px rgba(0,255,120,0)"
    ],
    filter: [
      "brightness(1) saturate(1)",
      "brightness(2.1) saturate(2.6)",
      "brightness(1) saturate(1)"
    ],
    duration: 520,
    easing: "easeOutQuad"
  });
}

let _waveTurnsShown = null;


function animateRollingInt(el, from, to, opts = {}){
  if (!el || !window.anime) return;

  const dur = opts.duration ?? 420;
  const easing = opts.easing ?? "easeOutExpo";

  // If first time or non-number: just set
  if (!Number.isFinite(from) || !Number.isFinite(to)){
    el.textContent = String(to ?? "—");
    return;
  }

  // Kill any running anim on this element
  window.anime.remove(el);

  // Animate a dummy value + update text each frame
  const obj = { v: from };

  // little "roll" feel: translateY + blur + punch
  window.anime.animate(el, {
    translateY: [0, -6, 0],
    scale: [1, 1.08, 1],
    duration: Math.min(260, dur),
    easing: "easeOutQuad"
  });

  window.anime.animate(obj, {
    v: to,
    duration: dur,
    easing,
    update: () => {
      el.textContent = String(Math.round(obj.v));
    }
  });
}


