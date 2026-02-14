/* game.js — consolidated (was inline in game.html) + old FX + smooth movement */

(function () {
  "use strict";

  /* ======================================================================
     ANIME (safe)
     ====================================================================== */
  // anime.js UMD exposes window.anime
  // Your old game.js had: const { animate } = anime;
  // Keep compatibility but don't crash if anime isn't loaded yet.
  const getAnime = () => (typeof window.anime === "function" ? window.anime : null);

  function animeAnimate(targets, params) {
    const anime = getAnime();
    if (!anime) return null;
    return anime({ targets, ...params });
  }


  /* ======================================================================
     GLOBALS (BEGIN)
     ====================================================================== */
  const IMG_BASE = "https://gameinfo.daraempire.com/wp-content/uploads/";
  const AVATAR_IMG_BASE = "https://gameinfo.daraempire.com/game-res/avatars";
  const BUFF_IMG_BASE = IMG_BASE;
  const SOUND_BASE = "https://gameinfo.daraempire.com/game-res/wav";
  const HELP_URL = "https://gameinfo.daraempire.com/ui/help.html";
  const IMG_EXT = ".png";

  let PLAYER_SCALE_MAX = 0.65;
  let PLAYER_SCALE_MIN_PERCENT = 0.20;

  let MOB_SIZE_NORMAL = 0.35;
  let MOB_SIZE_BOSS = 0.55;
  let MOB_SIZE_GROUPBOSS = 0.65;
  let MOB_SIZE_RAIDBOSS = 0.75;
  let MOB_SIZE_GROUPMOB = 0.40;
  let MOB_SIZE_RAIDMOB = 0.55;

  let waveOverSoundPlayed = false;
  const RANGED_FX_COOLDOWN_MS = 2000; // 2 seconds

  let INFO_SPEED_PX_PER_SEC = Number(localStorage.getItem("infoSpeedPxPerSec") || "80");

  const deadFx = new Map();
  const DEAD_STAY_MS = 2200;

  const mobDom = new Map();

  // Smooth movement to avoid "tuck tuck tuck"
  // Transition duration slightly below your poll interval (555ms)
  const SMOOTH_MOVE_MS = 520;

  function applySmoothMoveStyles(el) {
    if (!el) return;
    el.style.transition = `left ${SMOOTH_MOVE_MS}ms linear, top ${SMOOTH_MOVE_MS}ms linear, transform ${SMOOTH_MOVE_MS}ms linear`;
    el.style.willChange = "left, top, transform";
  }

  /* ======================================================================
     CONDITIONS
     ====================================================================== */
  const MOB_CONDITIONS = {
    Mezzed: { icon: "BuffMezzed.png", title: "Mezzed", desc: "Cannot act or attack" },
    Burned: { icon: "BuffBurned.png", title: "Burned", desc: "Takes fire damage over time and reduces defense" },
    Defused: { icon: "Defused.png", title: "Defused", desc: "Bomb is defused... not further action needed" },
    Poisoned: { icon: "BuffPoisoned.png", title: "Poisoned", desc: "Takes poison damage over time and reduces defense" },
    Wounded: { icon: "BuffWounded.png", title: "Wounded", desc: "Reduced effectiveness and reduces defense" },
    Defending: { icon: "BuffDefending.png", title: "Defending", desc: "Reduced damage taken" },
    Fleeing: { icon: "BuffFleeing.png", title: "Fleeing", desc: "Trying to escape" },
    Incapacitated: { icon: "BuffIncapacitated.png", title: "Incapacitated", desc: "Cannot act" }
  };

  const CONDITION_PRIORITY = [
    "Mezzed",
    "Burned",
    "Defused",
    "Poisoned",
    "Wounded",
    "Fleeing",
    "Defending",
    "Incapacitated"
  ];

  /* ======================================================================
     Mobile special behaviour(BEGIN)
     ====================================================================== */
  let _lastMobTapId = null;
  let _lastMobTapAt = 0;

  function isMobileLike() {
    return window.matchMedia("(pointer: coarse)").matches;
  }

  function tryAutoShootOnRepeatTap(mobId) {
    if (!isMobileLike()) return;

    const now = performance.now();
    const same = (_lastMobTapId === mobId);
    const fast = (now - _lastMobTapAt) <= 900;
    _lastMobTapId = mobId;
    _lastMobTapAt = now;

    if (!(same && fast)) return;

    const target = getActionTarget();
    if (!target) return;

    const shootBtn = document.querySelector(`.action-btn[data-action="shoot"]`);
    if (!shootBtn) return;
    if (shootBtn.classList.contains("cooldown")) return;

    shootBtn.click();
  }

  /* ======================================================================
     AUTH + STORAGE (BEGIN)
     ====================================================================== */
  const LOGIN_PAGE = "index.html";

  function getToken() { return localStorage.getItem("sessionToken") || ""; }
  function getGameId() { return localStorage.getItem("gameId") || "0"; }

  function readCharacterFromUrlOnce() {
    const qs = new URLSearchParams(window.location.search);
    const cid = (qs.get("characterId") || "").trim();
    const cname = (qs.get("characterName") || "").trim();

    if (cid) localStorage.setItem("characterId", cid);
    if (cname) localStorage.setItem("characterName", cname);

    if (cid || cname) {
      const clean = new URL(window.location.href);
      clean.searchParams.delete("characterId");
      clean.searchParams.delete("characterName");
      window.history.replaceState({}, "", clean.toString());
    }
  }

  
  function requireCharacterOrRedirect() {
    const cid = getMeCharacterId();
    const cname = getMeName();
    if (!cid && !cname) {
      window.location.href = LOGIN_PAGE;
    }
  }

  function authHeaders(extra = {}) {
    const t = getToken();
    if (!t) return extra;
    return { ...extra, "Authorization": "Bearer " + t };
  }

  function requireAuthOrRedirect() {
    const t = getToken();
    if (!t) window.location.href = LOGIN_PAGE;
  }

  /* ======================================================================
     API ENDPOINTS
     ====================================================================== */
  const ACTION_URL = "/api/v001/darawebgame/action";
  const STATE_URL = "/api/v001/darawebgame/state";
  const LOGOUT_URL = "/api/v001/darawebgame/auth/logout";

  /* ======================================================================
     Player stat tracking
     ====================================================================== */
  let lastPlayerStats = null;

  function resetPlayerStatTracking() {
    lastPlayerStats = null;
  }

  async function doLogout() {
    const token = getToken();
    try {
      document.getElementById("btnLogout")?.setAttribute("disabled", "disabled");
      document.getElementById("btnLogoutParty")?.setAttribute("disabled", "disabled");
    } catch (e) { }

    const clearLocal = () => {
      localStorage.removeItem("sessionToken");
      localStorage.removeItem("playerName");
      localStorage.removeItem("gameId");
      localStorage.removeItem("characterId");
      localStorage.removeItem("characterName");
    };

    if (!token) {
      clearLocal();
      window.location.href = LOGIN_PAGE;
      return;
    }

    resetPlayerStatTracking();

    try {
      const res = await fetch(LOGOUT_URL, {
        method: "POST",
        headers: authHeaders({ "Content-Type": "application/json" }),
        body: JSON.stringify({ gameId: String(getGameId()) })
      });

      if (!res.ok) {
        const txt = await res.text().catch(() => "");
        console.warn("Logout not OK:", res.status, txt);
      }
    } catch (e) {
      console.warn("Logout request failed (network):", e);
    } finally {
      clearLocal();
      window.location.href = LOGIN_PAGE;
    }
  }

  /* ======================================================================
     MEDIA + ACTIONS (BEGIN)
     ====================================================================== */
  const ACTION_COSTS = {
    attack: { res: "en", cost: 10 },
    defend: { res: "en", cost: 10 },
    fireball: { res: "mn", cost: 10 },
    heal: { res: "mn", cost: 10 },
    mezmerize: { res: "mn", cost: 10 },
    shoot: { res: "en", cost: 10 },
    usepotion: { res: null, cost: 0 },
    defuse: { res: "en", cost: 10 },
    revive: { res: "mn", cost: 10 },
    evac: { res: null, cost: 0 }
  };

  const MEDIA = {
    images: {
      attack: "https://gameinfo.daraempire.com/wp-content/uploads/2026/01/JumpAttack-150x150.png",
      defend: "https://gameinfo.daraempire.com/wp-content/uploads/2026/01/SpellShieldDefense-150x150.png",
      fireball: "https://gameinfo.daraempire.com/wp-content/uploads/2026/01/Fireball-150x150.png",
      mezmerize: "https://gameinfo.daraempire.com/wp-content/uploads/2026/01/SpellMezmerize-150x150.png",
      heal: "https://gameinfo.daraempire.com/wp-content/uploads/2026/01/PetComeToMe-150x150.png",
      revive: "https://gameinfo.daraempire.com/wp-content/uploads/2026/01/SpellBuffConstitution-150x150.png",
      evac: "https://gameinfo.daraempire.com/wp-content/uploads/2026/01/ModeratorEvac-150x150.png",
      defuse: "https://gameinfo.daraempire.com/wp-content/uploads/Defuse.jpg",
      usepotion: "https://gameinfo.daraempire.com/wp-content/uploads/2026/01/PotionManaExpert-150x150.png",
      shoot: "https://gameinfo.daraempire.com/wp-content/uploads/2026/01/Hitscan-150x150.png",
      dead: "https://gameinfo.daraempire.com/wp-content/uploads/Dead.png",
    }
  };

  const ACTION_SOUNDS = {
    attack: ["Attack.wav"],
    bombbeep: ["BombBeep.wav"],
    fireball: ["Fireball.wav"],
    heal: ["Heal.wav"],
    defend: ["Defense.wav"],
    explosion: ["Explosion1.wav", "Explosion2.wav"],
    explosionsmall: ["ExplosionSmall1.wav", "ExplosionSmall2.wav"],
    mezmerize: ["Mez.wav"],
    revive: ["Revive.wav"],
    evac: ["Evac.wav"],
    defuse: ["Defuse1.wav", "Defuse2.wav"],
    usepotion: ["Potion.wav"],
    shoot: ["LaserGun1.wav", "LaserGun2.wav", "LaserGun3.wav", "LaserGun4.wav"],
    mobdeath: ["Death_01.wav", "Death_02.wav", "Death_03.wav", "Death_04.wav", "Death_05.wav"],
    spiderdeath: ["MobDeath.wav", "SpiderDeath1.wav", "SpiderDeath2.wav"],
    gameover: ["GameOverSciFi.wav"],
    hit: ["PlayerHit1.wav", "PlayerHit2.wav", "PlayerHit3.wav"],
    loot: ["Loot1.wav", "Success_1.wav", "Success_2.wav", "Success_3.wav", "Success_4.wav", "Success_5.wav", "Success_6.wav", "Success_7.wav", "Success_8.wav", "Success_9.wav", "Success_10.wav"],
    waveover: ["Pad.wav"],
    wavebegin: ["WaveBegin.wav"]
  };

  function playSound(key) {
    const files = ACTION_SOUNDS[key];
    if (!files || files.length === 0) return;

    const randomFile = files[Math.floor(Math.random() * files.length)];
    try {
      const snd = new Audio(SOUND_BASE + "/" + randomFile);
      snd.play().catch(() => { });
    } catch (e) { }
  }

  function playSoundDeathForMob(mob) {
    if (Math.random() < 0.4) return;

    const fx = getMobEffects(mob);
    const atkKey = normKey(mob?.attackType);

    // priority: explicit config > fallback
    const deathKey =
      fx.deathSoundKey ||
      (atkKey === "spider" ? "spiderdeath" : "mobdeath");

    playSound(deathKey);
  }

  function triggerDeathEffect(mob)
  {
    const mobEntry = mobDom.get(mob.id);
    if (!mobEntry?.card) return;

    const fxCfg = getMobEffects(mob);
    if(!fxCfg.deathEffect) return;

    if(fxCfg.deathEffect=== "bomb"){
        spawnExplosionFxForMob(mob, mobEntry.card);
    }
  }

  const ACTIONS = [
    { id: "shoot", label: "Shoot", cd: 1 },
    { id: "fireball", label: "Fireball", cd: 5 },
    { id: "attack", label: "Attack", cd: 2 },
    { id: "defend", label: "Defend", cd: 3 },
    { id: "heal", label: "Heal", cd: 6 },
    { id: "mezmerize", label: "Mez", cd: 8 },
    { id: "usepotion", label: "Potion", cd: 10 },
    { id: "defuse", label: "Defuse", cd: 2 },
    { id: "revive", label: "Revive", cd: 12 },
    { id: "evac", label: "Evac", cd: 20 },
  ];

  /* DOM refs (set in startApp) */
  let actionsEl, actionTargetEl, actionMsgEl;
  let targetPillEl, targetPillImgEl, targetPillTextEl, targetPillClearEl;

  /* Cooldown state */
  const cooldownUntil = new Map();
  function nowMs() { return Date.now(); }

  function startCooldown(actionId, seconds) {
    cooldownUntil.set(actionId, nowMs() + seconds * 1000);
  }

  function getRemaining(actionId) {
    const until = cooldownUntil.get(actionId) || 0;
    return Math.max(0, until - nowMs());
  }

  function setButtonCooldownUI(btn, remainingMs, totalMs) {
    const timeEl = btn.querySelector(".cd-time");
    const fill = btn.querySelector(".cd-fill");

    if (remainingMs <= 0) {
      btn.classList.remove("cooldown");
      if (timeEl) timeEl.textContent = "";
      if (fill) fill.style.transform = "translateY(100%)";
      return;
    }

    btn.classList.add("cooldown");
    const sec = Math.ceil(remainingMs / 1000);
    if (timeEl) timeEl.textContent = String(sec);

    const pct = (remainingMs / totalMs) * 100;
    const y = 100 - pct;
    if (fill) fill.style.transform = `translateY(${y}%)`;
  }

  function buildButtons() {
    actionsEl.innerHTML = "";

    for (const a of ACTIONS) {
      const btn = document.createElement("button");
      btn.className = "action-btn";
      btn.type = "button";
      btn.dataset.action = a.id;
      btn.dataset.cd = String(a.cd);

      const img = document.createElement("img");
      img.src = MEDIA.images[a.id] || "";
      img.alt = a.id;
      btn.appendChild(img);

      const count = document.createElement("div");
      count.className = "count";
      btn.appendChild(count);

      const fill = document.createElement("div");
      fill.className = "cd-fill";
      btn.appendChild(fill);

      const overlay = document.createElement("div");
      overlay.className = "cd-overlay";
      overlay.innerHTML = `<div class="cd-time"></div>`;
      btn.appendChild(overlay);

      const tip = document.createElement("div");
      tip.className = "tip";
      tip.textContent = a.label;
      btn.appendChild(tip);

      btn.addEventListener("click", async () => {
        const actionId = a.id;
        const cdSec = a.cd;

        if (getRemaining(actionId) > 0) return;

        if (actionId === "usepotion") {
          const n = getPotionCount ? getPotionCount() : 0;
          if (Number(n) <= 0) {
            if (typeof showToast === "function") showToast("No potions left", "error", 1500);
            return;
          }
        }

        if (typeof canPayCost === "function") {
          const pay = canPayCost(actionId);
          if (pay && pay.ok === false) {
            const label = (typeof resLabel === "function") ? resLabel(pay.res) : (pay.res || "Resource");
            if (typeof showToast === "function") {
              showToast(`Not enough ${label} for ${actionId.toUpperCase()} (${pay.cur}/${pay.need})`, "error", 1700);
            }
            return;
          }
        }

        playSound(actionId);

        const characterId = getMeCharacterId();
        const characterName = getMeName();

        startCooldown(actionId, cdSec);
        tickCooldowns();

        const actionTarget = getActionTarget();
        const actionMsg = getActionMsg() || actionId;

        onPlayerDidAction(actionId, actionTarget);

        try {
          const res = await fetch(ACTION_URL, {
            method: "POST",
            headers: authHeaders({ "Content-Type": "application/json" }),
            body: JSON.stringify({
              gameId: String(getGameId()),
              characterId,
              characterName,
              actionId,
              actionTarget,
              actionMsg
            })
          });

          if (res.status === 401) doLogout();

          if (!res.ok && typeof showToast === "function") {
            const txt = await res.text().catch(() => "");
            const msg = txt ? txt.slice(0, 140) : `Action failed (${res.status})`;
            showToast(msg, "error", 1800);
          }
        } catch (e) {
          console.error("Action POST failed:", e);
          if (typeof showToast === "function") showToast("Network error: action not sent", "error", 1800);
        }
      });

      actionsEl.appendChild(btn);
    }
  }

  function tickCooldowns() {
    document.querySelectorAll(".action-btn").forEach(btn => {
      const actionId = btn.dataset.action;

      const totalMs = Number(btn.dataset.cd) * 1000;
      const remainingMs = getRemaining(actionId);
      setButtonCooldownUI(btn, remainingMs, totalMs);

      const countEl = btn.querySelector(".count");
      if (!countEl) return;

      if (actionId === "usepotion") {
        const n = getPotionCount();
        if (n > 0) {
          countEl.textContent = String(n);
          countEl.style.display = "flex";
        } else {
          countEl.style.display = "none";
        }
      } else {
        countEl.style.display = "none";
      }
    });
  }

  /* ======================================================================
     UI STATE + BASIC HELPERS
     ====================================================================== */
  function clamp01(x) { return Math.max(0, Math.min(1, x)); }
  function pct(v, max) { return max > 0 ? clamp01(v / max) : 0; }

  const ME={
    characterId:"",
    characterName: ""
  }

  let uiState = {
    turn: 1,
    mobs: [],
    party: [],
    selectedMobId: null,
    selectedPartyId: null,
  };

  function renderBadges() {
    const name = getMeName();
    const cid = getMeCharacterId() || "(no characterId)";
    const pb = document.getElementById("playerBadge");
    const gb = document.getElementById("gameBadge");
    const tb = document.getElementById("turnBadge");
    const wb = document.getElementById("waveBadge");
    const pc = document.getElementById("partyCount");

    if (pb) pb.textContent = `You: ${name}`;
    if (gb) gb.textContent = "Game: " + getGameId();
    if (tb) tb.textContent = "Turn: " + (uiState.turn ?? 1);
    if (wb) wb.textContent = "Wave: " + (uiState.wave ?? 0) + " | Left: " + (uiState.waveMobsLeft ?? 0);
    if (pc) pc.textContent = "Players: " + (uiState.party?.length ?? 0);
  }

  function escapeHtml(s) {
    return String(s).replace(/[&<>"']/g, c => ({
      "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#039;"
    }[c]));
  }

  function mobImageUrl(mobId) {
    if (!mobId) return "";
    const safe = mobId.replace(/[^a-zA-Z0-9_-]/g, "");
    return IMG_BASE + safe + IMG_EXT;
  }

  function playerImageUrl(avatarId) {
    if (!avatarId) return "";
    const safe = avatarId;
    return AVATAR_IMG_BASE + "/" + safe;
  }

  /* ======================================================================
     ENCOUNTER (NO FLICKER) - PERSISTENT DOM
     ====================================================================== */
  function ensureScene() {
    const lanesEl = document.getElementById("lanes");
    if (!lanesEl) return null;

    let scene = lanesEl.querySelector(".scene");
    if (scene) return scene;

    lanesEl.innerHTML = "";

    scene = document.createElement("div");
    scene.className = "scene";

    const battlefield = document.createElement("div");
    battlefield.className = "battlefield";

    const fog1 = document.createElement("div"); fog1.className = "fog";
    const fog2 = document.createElement("div"); fog2.className = "fog f2";

    scene.appendChild(battlefield);
    scene.appendChild(fog1);
    scene.appendChild(fog2);

    lanesEl.appendChild(scene);
    return scene;
  }

  function depthScale(y01) {
    return MOB_SIZE_NORMAL + clamp01(y01) * MOB_SIZE_NORMAL;
  }

  /* ======================================================================
     PLAYER CARD IN SCENE
     ====================================================================== */
  let lastKnownMe = null;
  let playerDomEntry = null;

  function createPlayerDom() {
    const scene = ensureScene();
    if (!scene) return null;
    const battlefield = scene.querySelector(".battlefield");

    const wrap = document.createElement("div");
    wrap.className = "playerAbs";
    wrap.dataset.id = "__player__";
    applySmoothMoveStyles(wrap);

    const card = document.createElement("div");
    card.className = "mobCard t2";

    const inner = document.createElement("div");
    inner.className = "mob";

    const tag = document.createElement("div");
    tag.className = "targetTag";
    tag.textContent = "You";

    const img = document.createElement("img");
    img.className = "mobImg";
    img.loading = "lazy";

    const fallback = document.createElement("div");
    fallback.className = "avatar";
    fallback.style.display = "none";
    fallback.textContent = "ME";

    const hpBar = document.createElement("div");
    hpBar.className = "hpBar";

    const hpFill = document.createElement("div");
    hpFill.className = "hpFill";
    hpBar.appendChild(hpFill);

    inner.appendChild(tag);
    inner.appendChild(img);
    inner.appendChild(fallback);
    inner.appendChild(hpBar);

    card.appendChild(inner);
    wrap.appendChild(card);
    battlefield.appendChild(wrap);

    img.onerror = () => {
      img.style.display = "none";
      fallback.style.display = "grid";
    };

    wrap.onclick = () => {
      uiState.selectedPartyId = "__me__";
      uiState.selectedMobId = null;

      updateEncounterMobs();
      try { updateEncounterPlayer(getMe()); } catch (e) { console.error("updateEncounterPlayer failed:", e); }
      renderParty();
      renderBadges();
    };

    playerDomEntry = { wrap, card, img, hpFill, tag, fallback };
    window.playerDomEntry = playerDomEntry; // keep compatibility for FX
    return playerDomEntry;
  }

  function calculatePlayerScale(
    data,
    y01,
    {
      minScale = PLAYER_SCALE_MIN_PERCENT,
      maxScale = 1.00,
      curve = 1.0
    } = {}
  ) {
    const baseScale = depthScale(y01) * PLAYER_SCALE_MAX;
    if (!data) return baseScale * maxScale;

    const maxHp = Number(data.hpMax || data.maxHP || 1);
    const hp = Number(data.hp || 0);

    if (maxHp <= 0) return baseScale * maxScale;

    const hpPct = Math.max(0, Math.min(1, hp / maxHp));
    const shaped = Math.pow(hpPct, curve);

    const hpScale = minScale + (maxScale - minScale) * shaped;
    return baseScale * hpScale;
  }

  function updateEncounterPlayer(mePlayer) {
    const scene = ensureScene();
    if (!scene) return;
    const rect = scene.getBoundingClientRect();
    const w = Math.max(1, rect.width);
    const h = Math.max(1, rect.height);
    const pad = 10;

    if (mePlayer) lastKnownMe = mePlayer;
    const data = mePlayer || lastKnownMe;

    const entry = playerDomEntry || createPlayerDom();
    if (!entry) return;

    const x01 = 0.5;
    const y01 = 0.92;

    const x = pad + x01 * (w - pad * 2);
    const y = pad + y01 * (h - pad * 2);

    entry.wrap.style.left = `${x}px`;
    entry.wrap.style.top = `${y}px`;

    const s = calculatePlayerScale(data, y01);
    entry.wrap.style.transform = `translate3d(-50%, -85%, 0) scale(${s})`;

    if (!data) {
      entry.tag.textContent = "Loading…";
      entry.hpFill.style.width = "100%";
      entry.img.style.display = "none";
      entry.fallback.style.display = "grid";
      entry.fallback.textContent = "…";
      entry.card.classList.remove("selected");
      return;
    }

    const max = Number(data.hpMax || data.maxHP || 1);
    const hp = Number(data.hp || 0);
    const hpPct = max > 0 ? clamp01(hp / max) * 100 : 0;
    entry.hpFill.style.width = `${hpPct}%`;

    const name = String(data.id || "You");
    entry.tag.textContent = name;

    const avatarId = data.avatarId || "";
    const imgSrc = playerImageUrl(avatarId);

    if (!imgSrc) {
      entry.img.removeAttribute("src");
      entry.img.style.display = "none";
      entry.fallback.style.display = "grid";
    } else if (entry.img.getAttribute("src") !== imgSrc) {
      entry.img.style.display = "";
      entry.fallback.style.display = "none";
      entry.img.src = imgSrc;
    }

    entry.fallback.textContent = (name.slice(0, 2) || "ME").toUpperCase();
    const selected = (uiState.selectedPartyId === "__me__");
    entry.card.classList.toggle("selected", selected);
  }

  /* ======================================================================
    MOB EFFECTS CONFIG (attackType + optional per-mob override)
    ====================================================================== */

  // Base config by attackType
  const MOB_EFFECTS_BY_ATTACKTYPE = {
    ranged: {
      projectile: "tracer",           // uses spawnRangedProjectile
      attackSoundKey: "shoot",        // you can change later
      idle: ["breathe"],              // placeholder for later
      rangedCooldownMs: 2000
    },
    melee: {
      projectile: null,               // no projectile
      attackSoundKey: null,
      idle: ["breathe"],
      rangedCooldownMs: 0
    },
    combi: {
      projectile: "tracer",
      attackSoundKey: "shoot",
      idle: ["breathe"],
      rangedCooldownMs: 2000
    },
    healer: {
      projectile: null,               // later: "healBeam" etc
      attackSoundKey: "heal",
      idle: ["breathe"],
      rangedCooldownMs: 0
    },

    // Your “special species as attackType” approach:
    spider: {
      projectile: "web",              // uses spawnWebProjectile
      attackSoundKey: null,           // or "shoot" if you want
      deathSoundKey: "spiderdeath",   // this replaces playSoundDeath special-case
      idle: ["breathe", "twitch"],
      rangedCooldownMs: 2000
    },
    insect: {
      projectile: "acid",             // uses spawnAcidSpit
      attackSoundKey: null,
      deathSoundKey: "mobdeath",
      idle: ["breathe"],
      rangedCooldownMs: 2000
    },
    bomb: {
      projectile: null,             // uses none
      bombCountdownSec: 15,
      attackSoundKey: null,
      deathSoundKey: "explosion",
      deathEffect: "bomb",
      idle: [],
      rangedCooldownMs: 2000
    }
  };

  // Optional hard overrides per mob “id” or “avatarId” (your choice)
  // Keying by mob.id is simplest because it’s stable in the UI.
  const MOB_EFFECTS_BY_MOBID = {
    // "QueenSpider": { projectile: "web", rangedCooldownMs: 1200, idle: ["breathe","twitch","pulseGlow"] },
    // "BomberDrone": { projectile: "tracer", attackSoundKey:"shoot" }
  };

  function getMobEffects(mob) {
    if (!mob) return {};

    const mobKey = String(mob.id || "").trim();
    const atkKey = normKey(mob.attackType);

    const base = MOB_EFFECTS_BY_ATTACKTYPE[atkKey] || {};
    const override = mobKey ? (MOB_EFFECTS_BY_MOBID[mobKey] || {}) : {};

    // merge with override priority
    return { ...base, ...override };
  }


  /* ======================================================================
     MOB DOM
     ====================================================================== */
  function createMobDom(mobId, idx) {
    const scene = ensureScene();
    if (!scene) return null;
    const battlefield = scene.querySelector(".battlefield");

    const wrap = document.createElement("div");
    wrap.className = "mobAbs";
    wrap.dataset.id = mobId;
    applySmoothMoveStyles(wrap);

    const card = document.createElement("div");
    card.className = "mobCard " + (["t1", "t2", "t3", "t4"][idx % 4]);

    card.dataset.mobId = mobId;
    card.classList.add("mob-card");

    const condWrap = document.createElement("div");
    condWrap.className = "mob-conditions";
    condWrap.style.display = "none";

    const condImgs = [];
    for (let k = 0; k < 3; k++) {
      const im = document.createElement("img");
      im.alt = "condition";
      im.style.display = "none";
      condWrap.appendChild(im);
      condImgs.push(im);
    }
    card.appendChild(condWrap);

    const mobInner = document.createElement("div");
    mobInner.className = "mob";

    const targetTag = document.createElement("div");
    targetTag.className = "targetTag";
    targetTag.textContent = mobId;

    const img = document.createElement("img");
    img.className = "mobImg";
    img.loading = "lazy";

    const avatarFallback = document.createElement("div");
    avatarFallback.className = "avatar";
    avatarFallback.style.display = "none";
    avatarFallback.textContent = mobId.slice(0, 3);

    const hpBar = document.createElement("div");
    hpBar.className = "hpBar";

    const hpFill = document.createElement("div");
    hpFill.className = "hpFill";
    hpBar.appendChild(hpFill);

    mobInner.appendChild(targetTag);
    mobInner.appendChild(img);
    mobInner.appendChild(avatarFallback);
    mobInner.appendChild(hpBar);

    card.appendChild(mobInner);
    wrap.appendChild(card);
    battlefield.appendChild(wrap);

    img.onerror = () => {
      img.style.display = "none";
      avatarFallback.style.display = "grid";
    };

    wrap.onclick = () => {
      uiState.selectedMobId = mobId;
      uiState.selectedPartyId = null;
      setTargetMobId(mobId);
      updateEncounterMobs();
      tryAutoShootOnRepeatTap(mobId);
    };

    const entry = { wrap, card, img, hpFill, targetTag, avatarFallback, condWrap, condImgs };
    mobDom.set(mobId, entry);
    return entry;
  }

  function updateEncounterMobs() {
    const scene = ensureScene();
    if (!scene) return;

    const rect = scene.getBoundingClientRect();
    const w = Math.max(1, rect.width);
    const h = Math.max(1, rect.height);
    const pad = 10;

    const now = Date.now();
    const alive = (uiState.mobs || []).filter(m => Number(m.hp) > 0);

    const corpses = [];
    for (const [id, fx] of deadFx) {
      if (fx.untilMs > now && fx.lastMob) corpses.push(fx.lastMob);
    }

    const mobs = [...alive, ...corpses];
    const seen = new Set();

    mobs.sort((a, b) => Number(a.y ?? 0.5) - Number(b.y ?? 0.5));

    for (let i = 0; i < mobs.length; i++) {
      const m = mobs[i];
      const id = String(m.id ?? "");
      if (!id) continue;

      seen.add(id);

      let entry = mobDom.get(id);
      if (!entry) entry = createMobDom(id, i);
      if (!entry) continue;

      const x01 = clamp01(Number(m.x ?? 0.5));
      const y01 = clamp01(Number(m.y ?? 0.5));

      const x = pad + x01 * (w - pad * 2);
      const y = pad + y01 * (h - pad * 2);

      entry.wrap.style.left = `${x}px`;
      entry.wrap.style.top = `${y}px`;

      const s = depthScale(y01);

      const diffKey = normKey(m.difficulty);
      entry.wrap.classList.remove(
        "diff-normal", "diff-boss", "diff-groupmob", "diff-groupboss", "diff-raidmob", "diff-raidboss",
        "isNonNormal"
      );

      const cls = diffKey ? ("diff-" + diffKey) : "diff-normal";
      entry.wrap.classList.add(cls);

      const isNonNormal = diffKey && diffKey !== "normal";
      entry.wrap.classList.toggle("isNonNormal", !!isNonNormal);

      let extra = 0;
      if (diffKey === "boss") extra = MOB_SIZE_BOSS;
      if (diffKey === "groupboss") extra = MOB_SIZE_GROUPBOSS;
      if (diffKey === "raidboss") extra = MOB_SIZE_RAIDBOSS;
      if (diffKey === "groupmob") extra = MOB_SIZE_GROUPMOB;
      if (diffKey === "raidmob") extra = MOB_SIZE_RAIDMOB;

      const finalScale = s * (1 + extra);
      entry.wrap.style.transform = `translate3d(-50%, -85%, 0) scale(${finalScale})`;
      entry.wrap.style.zIndex = String(Math.floor(y01 * 1000));

      // Conditions
      if (entry.condWrap && entry.condImgs) {
        const top = getTopConditions(m, 3);

        if (top.length > 0) {
          entry.condWrap.style.display = "flex";

          const all = getMobConditionsArray(m);
          entry.condWrap.title = all.map(c => {
            const info = conditionInfo(c);
            return `${info.title}: ${info.desc}`;
          }).join("\n");

          for (let k = 0; k < entry.condImgs.length; k++) {
            const im = entry.condImgs[k];
            const cname = top[k];

            if (!cname) {
              im.style.display = "none";
              im.removeAttribute("src");
              continue;
            }

            const info = conditionInfo(cname);
            const src = BUFF_IMG_BASE + info.icon;

            if (im.getAttribute("src") !== src) im.src = src;
            im.style.display = "";
          }
        } else {
          entry.condWrap.style.display = "none";
          entry.condWrap.title = "";
          for (const im of entry.condImgs) {
            im.style.display = "none";
            im.removeAttribute("src");
          }
        }
      }

      const max = Number(m.max || m.hpMax || 1);
      const hp = Number(m.hp || 0);
      const hpPct = max > 0 ? clamp01(hp / max) * 100 : 0;
      entry.hpFill.style.width = `${hpPct}%`;

      const label = String(m.displayName || m.avatarId || id);
      entry.targetTag.textContent = label;
      entry.avatarFallback.textContent = label.slice(0, 3);

      const isDead = hp <= 0;
      applyMobIdleFx(entry, m);
      applyBombFxIfNeeded(entry, m);

      entry.card.classList.toggle("dead", isDead);

      if (isDead && uiState.selectedMobId === id) uiState.selectedMobId = null;
      entry.card.classList.toggle("selected", uiState.selectedMobId === id);

      const threat = (uiState.selectedMobId === id) || (hpPct >= 75);
      entry.card.classList.toggle("threat", threat);

      let deathIcon = entry.card.querySelector(".deathIcon");
      if (isDead && !deathIcon) {
        deathIcon = document.createElement("div");
        deathIcon.className = "deathIcon";
        deathIcon.innerHTML = `<img src="${MEDIA.images.dead}" alt="dead" />`;
        entry.card.appendChild(deathIcon);
      } else if (!isDead && deathIcon) {
        deathIcon.remove();
      }

      const wantSrc = mobImageUrl(m.avatarId || id);
      if (entry.img.getAttribute("src") !== wantSrc) {
        entry.img.style.display = "";
        entry.avatarFallback.style.display = "none";
        entry.img.src = wantSrc;
      }
    }

    for (const [id, entry] of mobDom) {
      if (!seen.has(id)) {
        removeBombFx(id);
        entry.wrap.remove();
        mobDom.delete(id);
      }
    }
  }

  /* ======================================================================
     PARTY RENDER
     ====================================================================== */
  const partyDom = new Map();

  function ensurePartyRow(p) {
    const id = String(p.id || "");
    let entry = partyDom.get(id);
    if (entry) return entry;

    const row = document.createElement("div");
    row.className = "pRow";

    row.innerHTML = `
      <div class="pAvatar">
        <img class="avatarImg" alt="" decoding="async" loading="eager" />
        <div class="avatarText" style="display:none"></div>
      </div>

      <div class="pInfo">
        <div class="pNameLine">
          <span class="name"></span>
          <div style="display:flex; align-items:center; gap:8px;">
            <span class="lvlBadge"></span>
            <span class="meta"></span>
          </div>
        </div>

        <div class="pBars">
          <div class="pBarRow">
            <div style="font-weight:900;opacity:.9;">HP</div>
            <div class="pBar hp"><div class="fill"></div></div>
            <div class="val hpVal"></div>
          </div>

          <div class="pBarRow">
            <div style="font-weight:900;opacity:.9;">EN</div>
            <div class="pBar en"><div class="fill"></div></div>
            <div class="val enVal"></div>
          </div>

          <div class="pBarRow">
            <div style="font-weight:900;opacity:.9;">MN</div>
            <div class="pBar mn"><div class="fill"></div></div>
            <div class="val mnVal"></div>
          </div>

          <div class="pBarRow">
            <div style="font-weight:900;opacity:.9;">XP</div>
            <div class="pBar xp"><div class="fill"></div></div>
            <div class="val xpVal"></div>
          </div>
        </div>
      </div>
    `;

    const img = row.querySelector(".avatarImg");
    const fallback = row.querySelector(".avatarText");

    img.onerror = () => {
      img.style.display = "none";
      fallback.style.display = "grid";
    };

    row.onclick = () => {
      const name = (p.id || "").trim();
      if (!name) return;
      actionTargetEl.value = name;

      uiState.selectedPartyId = p.id;
      uiState.selectedMobId = null;

      updateEncounterMobs();
      renderParty();
      renderBadges();
    };

    entry = {
      row,
      img,
      fallback,
      nameEl: row.querySelector(".name"),
      lvlEl: row.querySelector(".lvlBadge"),
      metaEl: row.querySelector(".meta"),

      hpFill: row.querySelector(".pBar.hp .fill"),
      enFill: row.querySelector(".pBar.en .fill"),
      mnFill: row.querySelector(".pBar.mn .fill"),
      xpFill: row.querySelector(".pBar.xp .fill"),

      hpVal: row.querySelector(".hpVal"),
      enVal: row.querySelector(".enVal"),
      mnVal: row.querySelector(".mnVal"),
      xpVal: row.querySelector(".xpVal"),

      lastImgSrc: ""
    };

    partyDom.set(id, entry);
    document.getElementById("party")?.appendChild(row);
    return entry;
  }

  function setFill(el, percent01) {
    const w = Math.round(clamp01(Number(percent01) || 0) * 100);
    el.style.width = w + "%";
  }

  function renderParty() {
    const party = uiState.party || [];
    const seen = new Set();

    for (const p of party) {
      const id = String(p.id || "");
      if (!id) continue;
      seen.add(id);

      const e = ensurePartyRow(p);

      e.row.classList.toggle("selected", uiState.selectedPartyId === p.id);
      e.row.classList.toggle("damaged", !!(uiState._damagedIds && uiState._damagedIds.has(p.id)));

      e.nameEl.textContent = p.id || "";
      const lvl = Number(p.level ?? 0);
      e.lvlEl.textContent = `Lvl ${lvl}`;
      e.metaEl.textContent = p.active ? "ACTIVE" : "";

      const hpP = pct(p.hp, p.hpMax);
      const enP = pct(p.en, p.enMax);
      const mnP = pct(p.mn, p.mnMax);

      const xp = Number(p.xp ?? 0);
      const xpMax = getXpMax(p) || 100;
      const xpP = xpMax > 0 ? pct(xp, xpMax) : 0;

      setFill(e.hpFill, hpP);
      setFill(e.enFill, enP);
      setFill(e.mnFill, mnP);
      setFill(e.xpFill, xpP);

      e.hpVal.textContent = `${p.hp}/${p.hpMax}`;
      e.enVal.textContent = `${p.en}/${p.enMax}`;
      e.mnVal.textContent = `${p.mn}/${p.mnMax}`;
      e.xpVal.textContent = `${xp}/${xpMax}`;

      const url = playerImageUrl(p.avatarId);

      if (!url) {
        e.img.removeAttribute("src");
        e.img.style.display = "none";
        e.fallback.style.display = "grid";
        e.fallback.textContent = (id.slice(0, 2) || "??").toUpperCase();
      } else if (e.lastImgSrc !== url) {
        e.lastImgSrc = url;
        e.img.style.display = "";
        e.fallback.style.display = "none";
        e.img.src = url;
        e.fallback.textContent = (id.slice(0, 2) || "??").toUpperCase();
      }
    }

    for (const [id, e] of partyDom) {
      if (!seen.has(id)) {
        e.row.remove();
        partyDom.delete(id);
      }
    }
  }

  /* ======================================================================
     INFO MSG
     ====================================================================== */
  let lastInfoMsg = "";
  function updateInfoMarquee(msg) {
    const marquee = document.getElementById("infoMarquee");
    const a = document.getElementById("infoTextA");
    if (!marquee || !a) return;

    const text = String(msg || "").trim();
    const shown = text || "—";

    a.textContent = shown;
    marquee.title = text;

    marquee.classList.remove("scrolling");
    marquee.style.removeProperty("--infoShift");
    marquee.style.removeProperty("--infoDur");

    lastInfoMsg = text;
  }
  /* ======================================================================
     Condition Checks to adjust behaviour
     ====================================================================== */

  /* ======================================================================
     DAMAGE + DEATH DETECTION
     ====================================================================== */
  function extractPartyMap(state) {
    const map = new Map();
    (state?.party || []).forEach(p => map.set(p.id, p));
    return map;
  }

  function detectPartyDamage(prev, next) {
    const prevMap = extractPartyMap(prev);
    const out = [];

    for (const p of (next?.party || [])) {
      const id = p.id;
      const prevP = prevMap.get(id);
      if (!prevP) continue;

      const oldHp = Number(prevP.hp ?? 0);
      const newHp = Number(p.hp ?? 0);

      if (newHp < oldHp) out.push({ id, dmg: oldHp - newHp, newHp, oldHp });
    }
    return out;
  }

  function showToast(text, type = "info", ms = 1800) {
    const host = document.getElementById("lootToastHost");
    if (!host) return;

    const el = document.createElement("div");
    el.className = "lootToast";

    const t = String(type || "info").toLowerCase();
    if (t === "error") el.classList.add("toastError");
    else if (t === "warn" || t === "warning") el.classList.add("toastWarn");
    else if (t === "success") el.classList.add("toastSuccess");
    else if (t === "loot") el.classList.add("toastLoot");
    else el.classList.add("toastInfo");

    el.textContent = String(text || "—");
    el.style.animationDuration = `${Math.max(300, ms)}ms`;

    host.appendChild(el);
    setTimeout(() => el.remove(), Math.max(500, ms) + 50);
  }

  function showLootToast(text) {
    showToast(text, "loot", 2800);
    playSound("loot");
  }

  let lastState = null;
  const playedDeaths = new Set();

  function extractMobsMap(state) {
    const map = new Map();
    (state?.mobs || []).forEach(m => map.set(m.id, m));
    return map;
  }

  function detectMobDeaths(prev, next) {
    const prevMap = extractMobsMap(prev);
    const nextMap = extractMobsMap(next);
    const now = Date.now();

    for (const [id, m] of nextMap) {
      if (Number(m.hp) > 0) {
        playedDeaths.delete(id);
        deadFx.delete(id);
      }
    }

    const triggerDeathFx = (id, mobSnapshot) => {
      if (playedDeaths.has(id)) return;
      playedDeaths.add(id);

      playSoundDeathForMob(mobSnapshot);
      triggerDeathEffect(mobSnapshot);


      deadFx.set(id, {
        untilMs: now + DEAD_STAY_MS,
        lastMob: { ...mobSnapshot, hp: 0 }
      });

      markMobDeadAndRemove(id, DEAD_STAY_MS);
    };

    for (const [id, nextMob] of nextMap) {
      const prevMob = prevMap.get(id);
      const wasAlive = prevMob ? (Number(prevMob.hp) > 0) : true;
      const isDead = Number(nextMob.hp) <= 0;
      if (wasAlive && isDead) triggerDeathFx(id, nextMob);
    }

    for (const [id, prevMob] of prevMap) {
      const existsNow = nextMap.has(id);
      const wasAlive = Number(prevMob.hp) > 0;
      if (wasAlive && !existsNow) triggerDeathFx(id, prevMob);
    }

    for (const [id, fx] of deadFx) {
      if (now > fx.untilMs) deadFx.delete(id);
    }
  }

  function markMobDeadAndRemove(mobId, delayMs) {
    const card = document.querySelector(`.mob-card[data-mob-id="${CSS.escape(String(mobId))}"]`);
    if (!card) return;
    if (card.dataset.deadStarted === "1") return;

    card.dataset.deadStarted = "1";
    card.classList.add("is-dead");

    window.setTimeout(() => {
      card.remove();
    }, delayMs);
  }

  /* ======================================================================
     GAME OVER
     ====================================================================== */
  function showGameOverOverlay(reason, restartInMs) {
    const overlay = document.getElementById("gameOverOverlay");
    const reasonEl = document.getElementById("gameOverReason");
    const secEl = document.getElementById("gameOverSeconds");
    if (!overlay) return;

    if (reasonEl) reasonEl.textContent = reason || "You lost";
    const sec = Math.max(0, Math.ceil((Number(restartInMs) || 0) / 1000));
    if (secEl) secEl.textContent = String(sec);

    overlay.classList.remove("hidden");
    overlay.setAttribute("aria-hidden", "false");
    document.body.classList.add("locked");
  }

  function hideGameOverOverlay() {
    const overlay = document.getElementById("gameOverOverlay");
    if (!overlay) return;

    overlay.classList.add("hidden");
    overlay.setAttribute("aria-hidden", "true");
    document.body.classList.remove("locked");
  }

  /* ======================================================================
     RENDER ALL
     ====================================================================== */
  function renderAll() {
    renderBadges();
    updateEncounterPlayer(getMe());
    updateEncounterMobs();
    renderParty();
  }

  /* ======================================================================
     WAVE OVER overlay
     ====================================================================== */
  let _waveTurnsShown = null;

  function animateRollingInt(el, from, to, opts = {}) {
    const anime = getAnime();
    if (!el || !anime) return;

    const dur = opts.duration ?? 420;
    const easing = opts.easing ?? "easeOutExpo";

    if (!Number.isFinite(from) || !Number.isFinite(to)) {
      el.textContent = String(to ?? "—");
      return;
    }

    anime.remove(el);

    const obj = { v: from };

    animeAnimate(el, {
      translateY: [0, -6, 0],
      scale: [1, 1.08, 1],
      duration: Math.min(260, dur),
      easing: "easeOutQuad"
    });

    animeAnimate(obj, {
      v: to,
      duration: dur,
      easing,
      update: () => { el.textContent = String(Math.round(obj.v)); }
    });
  }

  function showWaveOverlay(wave, nextInTurns) {
    const el = document.getElementById("waveOverlay");
    const title = document.getElementById("waveTitle");
    const sub = document.getElementById("waveSub");
    const numEl = document.getElementById("waveTurnsNum");

    if (!el || !title) return;

    title.textContent = `WAVE ${Number(wave) || 0} CLEARED`;

    if (nextInTurns != null) {
      const turnsLeft = Math.max(0, Number(nextInTurns) || 0);
      if (numEl) {
        if (_waveTurnsShown === null) {
          numEl.textContent = String(turnsLeft);
        } else if (_waveTurnsShown !== turnsLeft) {
          animateRollingInt(numEl, _waveTurnsShown, turnsLeft, { duration: 520 });
        }
        _waveTurnsShown = turnsLeft;
      }
    } else {
      if (sub) sub.textContent = `Next wave incoming soon…`;
      if (numEl) numEl.textContent = "—";
      _waveTurnsShown = null;
    }

    el.classList.remove("hidden");
    el.setAttribute("aria-hidden", "false");
  }

  function hideWaveOverlay() {
    const el = document.getElementById("waveOverlay");
    if (!el) return;
    el.classList.add("hidden");
    el.setAttribute("aria-hidden", "true");
    _waveTurnsShown = null;
  }

    /* ======================================================================
     Anime functions
     ====================================================================== */
/* ======================================================================
   BOMB COUNTDOWN FX (anime.js) — attackType "bomb"
   ====================================================================== */

  const bombFxByMobId = new Map(); // mobId -> { rootEl, timerId, timeline, endAtMs }

  /* -----------------------------
    1) Separate explosion function
    ----------------------------- */
  function spawnExplosionFxForMob(mob, mobCardEl) {
      if (!mobCardEl) return;

      const r = mobCardEl.getBoundingClientRect();
      const x = r.left + r.width * 0.5;
      const y = r.top + r.height * 0.5;

      if(hasCondition(mob,"defused"))return;
      
      playSound("explosion");
      spawnDifficultyExplosion(mob, x, y);
  }

  function spawnDifficultyExplosion(mob, x, y) {
  if (!mob) return;

  const diff = String(mob.difficulty || "").toLowerCase();

  switch (diff) {

    // --------------------------------------------------
    // NORMAL
    // --------------------------------------------------
    case "normal":
      spawnExplosionFxAtXY(x, y, {
        sizePx: 120,
        sparkCount: 8,
        debrisCount: 5,
        lifeMs: 750
      });
      break;


    // --------------------------------------------------
    // GROUP MOB
    // --------------------------------------------------
    case "groupmob":
      spawnExplosionFxAtXY(x, y, {
        sizePx: 150,
        sparkCount: 12,
        debrisCount: 8,
        lifeMs: 850
      });
      break;


    // --------------------------------------------------
    // BOSS
    // --------------------------------------------------
    case "boss":
      spawnExplosionFxAtXY(x, y, {
        sizePx: 200,
        sparkCount: 18,
        debrisCount: 14,
        lifeMs: 1100
      });

      // second delayed burst
      setTimeout(() => {
        spawnExplosionFxAtXY(x, y, {
          sizePx: 260,
          sparkCount: 10,
          debrisCount: 6,
          soundKey: false,
          lifeMs: 900
        });
      }, 80);

      break;


    // --------------------------------------------------
    // GROUP BOSS
    // --------------------------------------------------
    case "groupboss":
      spawnExplosionFxAtXY(x, y, {
        sizePx: 260,
        sparkCount: 24,
        debrisCount: 18,
        lifeMs: 1200
      });

      setTimeout(() => {
        spawnExplosionFxAtXY(x, y, {
          sizePx: 320,
          sparkCount: 12,
          debrisCount: 8,
          soundKey: false,
          lifeMs: 1000
        });
      }, 120);

      break;


    // --------------------------------------------------
    // RAID MOB
    // --------------------------------------------------
    case "raidmob":
      spawnExplosionFxAtXY(x, y, {
        sizePx: 280,
        sparkCount: 26,
        debrisCount: 20,
        lifeMs: 1300
      });

      // triple shock
      setTimeout(() => {
        spawnExplosionFxAtXY(x, y, {
          sizePx: 340,
          sparkCount: 14,
          debrisCount: 10,
          soundKey: false,
          lifeMs: 1000
        });
      }, 90);

      setTimeout(() => {
        spawnExplosionFxAtXY(x, y, {
          sizePx: 200,
          sparkCount: 8,
          debrisCount: 6,
          soundKey: false,
          lifeMs: 700
        });
      }, 180);

      break;


    // --------------------------------------------------
    // RAID BOSS (NUCLEAR 😈)
    // --------------------------------------------------
    case "raidboss":
      spawnExplosionFxAtXY(x, y, {
        sizePx: 360,
        sparkCount: 40,
        debrisCount: 30,
        lifeMs: 1600
      });

      // expanding nuclear chain
      setTimeout(() => {
        spawnExplosionFxAtXY(x, y, {
          sizePx: 480,
          sparkCount: 20,
          debrisCount: 14,
          soundKey: false,
          lifeMs: 1400
        });
      }, 120);

      setTimeout(() => {
        spawnExplosionFxAtXY(x, y, {
          sizePx: 600,
          sparkCount: 10,
          debrisCount: 6,
          soundKey: false,
          shake: false,
          lifeMs: 1000
        });
      }, 240);

      break;


    // --------------------------------------------------
    // FALLBACK
    // --------------------------------------------------
    default:
      spawnExplosionFxAtXY(x, y);
  }
}


function spawnExplosionFxAtXY(x, y, opts = {}) {
    // expects your CSS classes in base.css:
    // .boomRoot .boomCore .boomRing .boomSmoke .boomSpark .boomDebris (+ keyframes)

    const layer = document.getElementById("fxLayer") || ensureFxLayer?.();
    if (!layer) return;

    const size = opts.sizePx ?? 140;

    // root
    const root = document.createElement("div");
    root.className = "boomRoot";
    root.style.left = `${x}px`;
    root.style.top  = `${y}px`;
    root.style.width  = `${size}px`;
    root.style.height = `${size}px`;

    // core / ring / smoke
    const core = document.createElement("div");
    core.className = "boomCore";

    const ring = document.createElement("div");
    ring.className = "boomRing";

    const smoke = document.createElement("div");
    smoke.className = "boomSmoke";

    root.appendChild(core);
    root.appendChild(ring);
    root.appendChild(smoke);

    // sparks (thin streaks)
    const sparkCount = opts.sparkCount ?? 10;
    for (let i = 0; i < sparkCount; i++) {
      const s = document.createElement("div");
      s.className = "boomSpark";

      const rot = Math.random() * 360;
      const dist = (size * 0.55) + Math.random() * (size * 0.55); // how far it flies
      const dx = Math.cos(rot * Math.PI / 180) * dist;
      const dy = Math.sin(rot * Math.PI / 180) * dist;

      s.style.setProperty("--rot", `${rot}deg`);
      s.style.setProperty("--dx", `${dx}px`);
      s.style.setProperty("--dy", `${dy}px`);

      // small per-spark delay makes it feel more chaotic
      s.style.animationDelay = `${Math.random() * 60}ms`;

      root.appendChild(s);
    }

    // debris (chunks)
    const debrisCount = opts.debrisCount ?? 7;
    for (let i = 0; i < debrisCount; i++) {
      const d = document.createElement("div");
      d.className = "boomDebris";

      const rot = Math.random() * 360;
      const dist = (size * 0.35) + Math.random() * (size * 0.65);
      const dx = Math.cos(rot * Math.PI / 180) * dist;
      const dy = Math.sin(rot * Math.PI / 180) * dist;

      // random chunk size + spin
      const s = 0.7 + Math.random() * 1.1;              // scale
      const spin = (-220 + Math.random() * 440) + "deg"; // -220..220

      d.style.setProperty("--dx", `${dx}px`);
      d.style.setProperty("--dy", `${dy}px`);
      d.style.setProperty("--s", `${s}`);
      d.style.setProperty("--spin", `${spin}`);

      // optional: slightly different chunk sizes (w/h)
      d.style.width = `${8 + Math.random() * 10}px`;
      d.style.height = `${5 + Math.random() * 8}px`;

      d.style.animationDelay = `${Math.random() * 80}ms`;

      root.appendChild(d);
    }

    layer.appendChild(root);

    // optional sound
    if (opts.soundKey !== false) {
      try { playSound("explosion"); } catch {}
    }

    // optional little screenshake + flash (if you want)
    if (opts.shake !== false) {
      document.body.classList.add("hitShake");
      setTimeout(() => document.body.classList.remove("hitShake"), 260);
    }

    // cleanup after the longest animation (debris is 760ms)
    setTimeout(() => root.remove(), opts.lifeMs ?? 900);
  }

  /* -----------------------------
   2) Optional: add styles once
   ----------------------------- */
  
  function removeBombFx(mobId) {
    const fx = bombFxByMobId.get(mobId);
    if (!fx) return;

    try { if (fx.timerId) clearInterval(fx.timerId); } catch (e) {}
    try { if (fx.soundTimerId) clearInterval(fx.soundTimerId);} catch (e) {}
    try { fx.rootEl?.remove(); } catch (e) {}
    try { fx.rootEl?.remove(); } catch (e) {}
    


    bombFxByMobId.delete(mobId);
  }

/* -----------------------------
   4) Your upgraded countdown function
   ----------------------------- */
function spawnBombCountdownFxForMob(mob, mobCardEl) {
    const anime = window.anime;
    if (!anime || !mob || !mobCardEl) return;

    const mobId = String(mob.id || "").trim();
    if (!mobId) return;
    if(hasCondition(mob,"defused"))return;
    console.log("BOMB defused 1")

    // if already running -> keep it
    if (bombFxByMobId.has(mobId)) return;
 
    const fxCfg = getMobEffects(mob);
    const totalSec = Math.max(3, Number(fxCfg.bombCountdownSec) || 20);

    // Root element mounted inside the mobCard so it moves/scales with it
    const root = document.createElement("div");
    root.className = "bombFx";

    const ring = document.createElement("div");
    ring.className = "ring";

    const fill = document.createElement("div");
    fill.className = "fill";

    const num = document.createElement("div");
    num.className = "num";

    const label = document.createElement("div");
    label.className = "label";
    label.textContent = "BOMB";

    root.appendChild(fill);
    root.appendChild(ring);
    root.appendChild(num);
    root.appendChild(label);

    // Ensure mobCard is positioning context
    const prevPos = getComputedStyle(mobCardEl).position;
    if (prevPos === "static") mobCardEl.style.position = "relative";

    mobCardEl.appendChild(root);

    const startedAtMs = Date.now();
    const endAtMs = startedAtMs + totalSec * 1000;

    // Flag that is only true if the countdown reached 0 naturally
    let finishedNaturally = false;

    // ---- Sound every 5 seconds while active ----
    // (plays immediately, then every 5s; change if you want first beep after 5s)
    const soundTimerId = setInterval(() => {
      // only play if still active
      if (!bombFxByMobId.has(mobId)) return;
      const entry = bombFxByMobId.get(mobId);
      const mobNow = entry?.mobRef || mob; // see below note
      if (hasCondition(mobNow, "defused")) {
        console.log("BOMB defused 2")
        clearInterval(entry.timerId);
        clearInterval(entry.soundTimerId);
        removeBombFx(mobId);      // removes UI + deletes map entry
        return;
      }
      playSound("bombbeep"); // <-- your sound key
    }, 5000);

    // Optional: play right away too
    playSound("bombbeep");

    // Tick updater
    const timerId = setInterval(() => {
      const entry = bombFxByMobId.get(mobId);
      const mobNow = entry?.mobRef || mob;

      // ✅ stop if defused
      if (hasCondition(mobNow, "defused")) {
        console.log("BOMB defused 3")
        clearInterval(timerId);
        clearInterval(soundTimerId);
        removeBombFx(mobId);
        return;
      }

      const leftMs = Math.max(0, endAtMs - Date.now());
      const leftSec = Math.ceil(leftMs / 1000);

      num.textContent = String(leftSec);

      // Update conic fill
      const done = 1 - (leftMs / (totalSec * 1000));
      const deg = Math.max(0, Math.min(360, done * 360));
      fill.style.background = `conic-gradient(rgba(255,70,70,0.95) ${deg}deg, rgba(255,70,70,0.15) 0deg)`;

      // Last 5 seconds: stronger pulse
      if (leftSec <= 5) {
        root.style.filter = "drop-shadow(0 0 14px rgba(255,60,60,0.6))";
      }

      // Done
      if (leftMs <= 0) {
        finishedNaturally = true;

        clearInterval(timerId);
        clearInterval(soundTimerId);

        // one last flash pop
        root.classList.add("flash");

        setTimeout(() => removeBombFx(mobId), 240);
      }
    }, 120);

    bombFxByMobId.set(mobId, {
      rootEl: root,
      timerId,
      soundTimerId,
      timeline: null,
      endAtMs
    });
    // ✅ Important: store a reference you update each state tick (see next section)
    mobRef: mob
  }

  /* -----------------------------
   5) Your existing apply function can stay the same
   ----------------------------- */
  function applyBombFxIfNeeded(entry, mob) {
    if (!entry?.card || !mob) return;

    const atk = normKey(mob.attackType);
    const isDead = Number(mob.hp) <= 0;

    // stop if not bomb or dead
    if (atk !== "bomb" || isDead) {
      removeBombFx(String(mob.id || "").trim());
      return;
    }

    // start if needed
    spawnBombCountdownFxForMob(mob, entry.card);
  }



  /**** END BOMB **********************************************************/

  /* ======================================================================
     Target Pill functions
     ====================================================================== */
  function normKey(s) {
    return String(s || "")
      .trim()
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, "");
  }

  function mapAttackType(s) {
    const k = normKey(s);
    switch (k) {
      case "ranged": return { text: "Ranged", cls: "tpAtk-ranged" };
      case "melee": return { text: "Melee", cls: "tpAtk-melee" };
      case "combi": return { text: "Ranged/Melee", cls: "tpAtk-combi" };
      case "healer": return { text: "Healer", cls: "tpAtk-healer" };
      default: return { text: (s ? String(s).toLowerCase() : "—"), cls: "" };
    }
  }

  function mapDifficulty(s) {
    const k = normKey(s);
    switch (k) {
      case "normal": return { text: "Normal", cls: "tpDiff-normal" };
      case "boss": return { text: "Boss", cls: "tpDiff-boss" };
      case "groupmob": return { text: "Group Mob", cls: "tpDiff-groupmob" };
      case "groupboss": return { text: "Group Boss", cls: "tpDiff-groupboss" };
      case "raidmob": return { text: "Raid Mob", cls: "tpDiff-raidmob" };
      case "raidboss": return { text: "Raid Boss", cls: "tpDiff-raidboss" };
      default: return { text: (s ? String(s).toLowerCase() : "—"), cls: "" };
    }
  }

  function setBadge(el, mapping, prefix) {
    if (!el) return;
    el.classList.forEach(c => { if (c.startsWith(prefix)) el.classList.remove(c); });
    el.textContent = mapping.text;
    if (mapping.cls) el.classList.add(mapping.cls);
  }

  /* ======================================================================
     Helper Functions
     ====================================================================== */
  function getMe() {
    const party = uiState.party || [];

    const cid = getMeCharacterId();
    if (cid) {
      const hit = party.find(p => String(p.characterId || "").trim() === cid);
      if (hit) return hit;
    }
    return null;
  }

  function getResourceValue(player, res) {
    if (!player) return 0;
    if (res === "en") return Number(player.en ?? 0);
    if (res === "mn") return Number(player.mn ?? 0);
    return 0;
  }

  function resLabel(res) {
    return res === "en" ? "Energy" : (res === "mn" ? "Mana" : "Resource");
  }

  function canPayCost(actionId) {
    const rule = ACTION_COSTS[actionId];
    if (!rule || !rule.res || Number(rule.cost) <= 0) return { ok: true };

    const me = getMe();
    const cur = getResourceValue(me, rule.res);
    const need = Number(rule.cost);

    return { ok: cur >= need, res: rule.res, cur, need };
  }

  function getPotionCount() {
    const me = getMe();
    return Number(me?.potions ?? 0);
  }

  function getXpMax(p) {
    return Number(100);
  }

  function getMobById(id) {
    return (uiState.mobs || []).find(m => String(m.id) === String(id)) || null;
  }

  function getActionTarget() { return (actionTargetEl?.value || "").trim(); }
  function getActionMsg() { return (actionMsgEl?.value || "").trim(); }

  function setTargetMobId(mobId) {
    const id = String(mobId || "").trim();
    actionTargetEl.value = id;
    actionTargetEl.dataset.targetId = id;
    renderTargetPillFromId(id);
  }

  function clearTarget() {
    actionTargetEl.value = "";
    delete actionTargetEl.dataset.targetId;
    renderTargetPillFromId("");
  }

  function renderTargetPillFromId(id) {
    const mob = id ? getMobById(id) : null;

    const label = mob ? String(mob.displayName || mob.avatarId || mob.id) : "No Target";
    const imgSrc = mob ? mobImageUrl(mob.avatarId || mob.id) : "";

    const tpAttackEl = document.getElementById("tpAttack");
    const tpDiffEl = document.getElementById("tpDiff");

    if (mob) {
      const atk = mapAttackType(mob.attackType);
      const dif = mapDifficulty(mob.difficulty);
      setBadge(tpAttackEl, atk, "tpAtk-");
      setBadge(tpDiffEl, dif, "tpDiff-");
    } else {
      setBadge(tpAttackEl, { text: "—", cls: "" }, "tpAtk-");
      setBadge(tpDiffEl, { text: "—", cls: "" }, "tpDiff-");
    }

    if (targetPillTextEl) targetPillTextEl.textContent = label;
    if (targetPillEl) targetPillEl.title = mob ? `${label} (${mob.id})` : "No Target";

    if (targetPillImgEl) {
      if (imgSrc) {
        targetPillImgEl.style.display = "";
        targetPillImgEl.src = imgSrc;
        targetPillImgEl.onerror = () => { targetPillImgEl.style.display = "none"; };
      } else {
        targetPillImgEl.style.display = "none";
        targetPillImgEl.removeAttribute("src");
      }
    }

    if (targetPillClearEl) {
      targetPillClearEl.style.display = id ? "" : "none";
    }
  }

  function isMob(targetId) {
    if (!targetId) return false;
    const id = String(targetId).trim();
    if (!id) return false;
    return (uiState.mobs || []).some(m => String(m.id) === id);
  }

  function CheckTargetClearNeeded() {
    const curTarget = getActionTarget();
    if (!curTarget) return;
    if (!isMob(curTarget)) return;

    const m = getMobById(curTarget);
    if (!m || Number(m.hp) <= 0) {
      clearTarget();
      uiState.selectedMobId = null;
    }
  }

  // conditions
  function getMobConditionsArray(mob) {
    const arr = mob?.conditions;
    if (Array.isArray(arr)) return arr.filter(Boolean);

    const one = mob?.condition;
    if (typeof one === "string" && one && one !== "None") return [one];
    return [];
  }

  function hasCondition(mob, name) {
    const list = getMobConditionsArray(mob);
    const n = String(name || "").trim().toLowerCase();
    return list.some(c => String(c).trim().toLowerCase() === n);
  }

  function isMezzed(mob) {
    return hasCondition(mob, "Mezzed") || hasCondition(mob, "Incapacitated");
  }

  function getTopConditions(mob, maxCount = 3) {
    const list = getMobConditionsArray(mob);
    if (!list.length) return [];

    const set = new Set(list.map(c => String(c).trim()).filter(Boolean));
    const unique = Array.from(set);

    unique.sort((a, b) => {
      const ia = CONDITION_PRIORITY.findIndex(x => x.toLowerCase() === a.toLowerCase());
      const ib = CONDITION_PRIORITY.findIndex(x => x.toLowerCase() === b.toLowerCase());
      const pa = ia === -1 ? 999 : ia;
      const pb = ib === -1 ? 999 : ib;
      if (pa !== pb) return pa - pb;
      return a.localeCompare(b);
    });

    return unique.slice(0, Math.max(0, maxCount | 0));
  }

  function conditionInfo(condName) {
    const key = String(condName || "").trim();
    return MOB_CONDITIONS[key] || {
      icon: "icon-unknown.png",
      title: key || "Unknown",
      desc: "Unknown condition"
    };
  }

  /* ======================================================================
     HELP
     ====================================================================== */
  function openHelp() {
    window.open(HELP_URL, "_blank", "noopener");
  }
  window.openHelp = openHelp;

  /* ======================================================================
     FX layer helpers
     ====================================================================== */
  function ensureFxLayer() {
    let layer = document.getElementById("fxLayer");
    if (!layer) {
      layer = document.createElement("div");
      layer.id = "fxLayer";
      document.body.appendChild(layer);
    }
    return layer;
  }

  function getPlayerCardCenter() {
    const el = (playerDomEntry && playerDomEntry.wrap) ? playerDomEntry.wrap : document.querySelector(".playerAbs");
    if (!el) return null;
    const r = el.getBoundingClientRect();
    return { x: r.left + r.width * 0.5, y: r.top + r.height * 0.5 };
  }

  /* Ranged projectile */
  function spawnRangedProjectile(fromEl) {
    if (!fromEl) return;
    const layer = document.getElementById("fxLayer") || ensureFxLayer();
    if (!layer) return;

    const a = fromEl.getBoundingClientRect();
    const x1 = a.left + a.width * 0.75;
    const y1 = a.top + a.height * 0.35;

    const pc = getPlayerCardCenter();
    const x2 = pc ? pc.x : (window.innerWidth * 0.5);
    const y2 = pc ? (pc.y - 6) : (window.innerHeight * 0.86);

    const dx = x2 - x1;
    const dy = y2 - y1;
    const dist = Math.hypot(dx, dy);
    const angle = Math.atan2(dy, dx) * 180 / Math.PI;

    const tracer = document.createElement("div");
    tracer.className = "fx-tracer";
    tracer.style.left = x1 + "px";
    tracer.style.top = y1 + "px";
    tracer.style.width = dist + "px";
    tracer.style.transform = `translate(0,-50%) rotate(${angle}deg)`;
    layer.appendChild(tracer);

    const shot = document.createElement("div");
    shot.className = "fx-shot";
    shot.style.left = x1 + "px";
    shot.style.top = y1 + "px";
    layer.appendChild(shot);

    shot.animate(
      [{ left: x1 + "px", top: y1 + "px" }, { left: x2 + "px", top: y2 + "px" }],
      { duration: 160, easing: "linear" }
    ).onfinish = () => {
      shot.remove();
      const hit = document.createElement("div");
      hit.className = "fx-hit";
      hit.style.left = x2 + "px";
      hit.style.top = y2 + "px";
      layer.appendChild(hit);
      setTimeout(() => hit.remove(), 260);
    };

    tracer.animate([{ opacity: 0.9 }, { opacity: 0 }], { duration: 160, easing: "linear" })
      .onfinish = () => tracer.remove();
  }

  function spawnWebProjectile(fromEl) {
    if (!fromEl) return;

    const layer = document.getElementById("fxLayer") || ensureFxLayer();
    if (!layer) return;

    const a = fromEl.getBoundingClientRect();
    const x1 = a.left + a.width * 0.75;
    const y1 = a.top + a.height * 0.35;

    const pc = getPlayerCardCenter();
    const x2 = pc ? pc.x : (window.innerWidth * 0.5);
    const y2 = pc ? pc.y : (window.innerHeight * 0.86);

    const dx = x2 - x1;
    const dy = y2 - y1;
    const dist = Math.hypot(dx, dy);
    const angle = Math.atan2(dy, dx) * 180 / Math.PI;

    const dur = Math.min(420, Math.max(220, dist * 0.35));

    const strand = document.createElement("div");
    strand.className = "fx-webstrand";
    strand.style.left = x1 + "px";
    strand.style.top = y1 + "px";
    strand.style.width = dist + "px";
    strand.style.transform = `translate(0,-50%) rotate(${angle}deg)`;
    layer.appendChild(strand);

    const ball = document.createElement("div");
    ball.className = "fx-webball";
    ball.style.left = x1 + "px";
    ball.style.top = y1 + "px";
    layer.appendChild(ball);

    ball.animate([{ left: x1 + "px", top: y1 + "px" }, { left: x2 + "px", top: y2 + "px" }],
      { duration: dur, easing: "cubic-bezier(.2,.8,.2,1)" }
    ).onfinish = () => {
      ball.remove();
      const splat = document.createElement("div");
      splat.className = "fx-websplat";
      splat.style.left = x2 + "px";
      splat.style.top = y2 + "px";
      layer.appendChild(splat);
      setTimeout(() => splat.remove(), 560);
    };

    strand.animate([{ opacity: 0.85 }, { opacity: 0 }], { duration: dur, easing: "ease-out" })
      .onfinish = () => strand.remove();
  }

  function spawnAcidSpit(fromEl) {
    if (!fromEl) return;
    const layer = document.getElementById("fxLayer") || ensureFxLayer();
    if (!layer) return;

    const a = fromEl.getBoundingClientRect();
    const x1 = a.left + a.width * 0.70;
    const y1 = a.top + a.height * 0.40;

    const pc = getPlayerCardCenter();
    const x2 = pc ? pc.x : (window.innerWidth * 0.5);
    const y2 = pc ? pc.y : (window.innerHeight * 0.86);

    const dx = x2 - x1;
    const dy = y2 - y1;
    const dist = Math.hypot(dx, dy);
    const angle = Math.atan2(dy, dx) * 180 / Math.PI;

    const dur = Math.min(520, Math.max(240, dist * 0.45));

    const trail = document.createElement("div");
    trail.className = "fx-acidtrail";
    trail.style.left = x1 + "px";
    trail.style.top = y1 + "px";
    trail.style.width = dist + "px";
    trail.style.transform = `translate(0,-50%) rotate(${angle}deg)`;
    layer.appendChild(trail);

    const glob = document.createElement("div");
    glob.className = "fx-acidglob";
    glob.style.left = x1 + "px";
    glob.style.top = y1 + "px";
    layer.appendChild(glob);

    const mx = (x1 + x2) * 0.5;
    const my = (y1 + y2) * 0.5 - Math.min(60, dist * 0.15);

    glob.animate(
      [
        { transform: "translate(-50%,-50%) scale(0.9)", offset: 0, left: x1 + "px", top: y1 + "px" },
        { transform: "translate(-50%,-50%) scale(1.1)", offset: 0.5, left: mx + "px", top: my + "px" },
        { transform: "translate(-50%,-50%) scale(1.0)", offset: 1, left: x2 + "px", top: y2 + "px" }
      ],
      { duration: dur, easing: "cubic-bezier(.2,.8,.2,1)" }
    ).onfinish = () => {
      glob.remove();
      const splash = document.createElement("div");
      splash.className = "fx-acidsplash";
      splash.style.left = x2 + "px";
      splash.style.top = y2 + "px";
      splash.style.transform = `translate(-50%,-50%) rotate(${(Math.random() * 18 - 9).toFixed(1)}deg)`;
      layer.appendChild(splash);
      setTimeout(() => splash.remove(), 600);
    };

    trail.animate([{ opacity: 0.9 }, { opacity: 0 }], { duration: dur, easing: "ease-out" })
      .onfinish = () => trail.remove();
  }

  let lastMobFxAt = 0;

  function handleMobAttackFx(mob) {
    if (!mob) return;
    if (isMezzed(mob)) return;

    const fx = getMobEffects(mob);
    const cd = Number(fx.rangedCooldownMs ?? RANGED_FX_COOLDOWN_MS);

    // If the mob has no “attack FX”, do nothing.
    if (!fx.projectile) return;

    const now = performance.now();
    if (cd > 0 && (now - lastMobFxAt) < cd) return;
    lastMobFxAt = now;

    const mobWrap = document.querySelector(`.mobAbs[data-id="${CSS.escape(String(mob.id))}"]`);
    if (!mobWrap) return;

    switch (fx.projectile) {
      case "tracer":
        spawnRangedProjectile(mobWrap);
        if (fx.attackSoundKey) playSound(fx.attackSoundKey);
        break;

      case "web":
        spawnWebProjectile(mobWrap);
        if (fx.attackSoundKey) playSound(fx.attackSoundKey);
        break;

      case "acid":
        spawnAcidSpit(mobWrap);
        if (fx.attackSoundKey) playSound(fx.attackSoundKey);
        break;
    }
  } //handleMobAttackFx

  function applyMobIdleFx(entry, mob) {
    if (!entry?.card || !mob) return;

      const isDead = Number(mob.hp) <= 0;
      const fx = getMobEffects(mob);

      // Remove all idle classes first (clean state)
      entry.card.classList.remove(
        "idle-breathe",
        "idle-spider",
        "idle-insect"
      );

      // If dead → no idle ever
      if (isDead) return;

      // If no idle config → do nothing
      if (!Array.isArray(fx.idle) || fx.idle.length === 0) return;

      // Apply configured idles
      if (fx.idle.includes("breathe")) {
        entry.card.classList.add("idle-breathe");
      }

      if (fx.idle.includes("spider")) {
        entry.card.classList.add("idle-spider");
      }

      if (fx.idle.includes("insect")) {
        entry.card.classList.add("idle-insect");
      }
  }



  /* ======================================================================
     Local action FX (shoot/fireball etc)
     ====================================================================== */
  function getElCenter(el) {
    const r = el.getBoundingClientRect();
    return { x: r.left + r.width / 2, y: r.top + r.height / 2 };
  }

  function getPlayerCardEl() {
    return document.querySelector(".playerAbs .mobCard") || document.querySelector(".playerAbs");
  }

  function getMobCardElById(mobId) {
    const id = CSS.escape(String(mobId));
    return document.querySelector(`.mobAbs[data-id="${id}"] .mobCard`)
      || document.querySelector(`.mobCard[data-mob-id="${id}"]`)
      || document.querySelector(`.mobAbs[data-id="${id}"]`);
  }

  function fxShoot(fromPt, toPt, opts = {}) {
    const layer = ensureFxLayer();
    const el = document.createElement("div");
    el.className = "fx-bullet";

    const dx = toPt.x - fromPt.x;
    const dy = toPt.y - fromPt.y;
    const len = Math.hypot(dx, dy);
    const ang = Math.atan2(dy, dx) * 180 / Math.PI;

    el.style.left = `${fromPt.x}px`;
    el.style.top = `${fromPt.y}px`;
    el.style.width = `${Math.max(12, len)}px`;
    el.style.transform = `translate(0, -50%) rotate(${ang}deg)`;

    layer.appendChild(el);

    const dur = opts.duration ?? 180;
    el.animate([{ opacity: 0.95 }, { opacity: 0 }], { duration: dur, easing: "ease-out", fill: "forwards" });
    setTimeout(() => el.remove(), dur + 30);
  }

  function spawnFireballImpact(layer, x, y) {
    const host = document.createElement("div");
    host.className = "fx-impact-fireball";
    host.style.left = x + "px";
    host.style.top = y + "px";

    host.innerHTML = `
      <div class="boom"></div>
      <div class="ring"></div>
      ${Array.from({ length: 8 }).map((_, i) => {
      const deg = Math.round((360 / 8) * i + (Math.random() * 18 - 9));
      return `<div class="spark" style="--r:${deg}deg"></div>`;
    }).join("")}
    `;

    layer.appendChild(host);
    setTimeout(() => host.remove(), 450);
  }

  function fxFireball(fromPt, toPt, opts = {}) {
    const layer = ensureFxLayer();
    const ball = document.createElement("div");
    ball.className = "fx-fireball";
    layer.appendChild(ball);

    const dx = toPt.x - fromPt.x;
    const dy = toPt.y - fromPt.y;
    const dist = Math.hypot(dx, dy);

    const base = opts.duration ?? 520;
    const dur = Math.min(900, Math.max(380, base + dist * 0.15));

    const ang = Math.atan2(dy, dx) * 180 / Math.PI;

    const anim = ball.animate(
      [
        {
          transform:
            `translate(${fromPt.x}px, ${fromPt.y}px)
             translate(-50%, -50%)
             rotate(${ang}deg)
             scale(0.9)`
        },
        {
          transform:
            `translate(${toPt.x}px, ${toPt.y}px)
             translate(-50%, -50%)
             rotate(${ang}deg)
             scale(1.1)`
        }
      ],
      { duration: dur, easing: "cubic-bezier(.2,.9,.2,1)", fill: "forwards" }
    );

    const wob = ball.animate(
      [
        { filter: "drop-shadow(0 0 10px rgba(255,120,30,0.65))" },
        { filter: "drop-shadow(0 0 16px rgba(255,120,30,0.95))" },
        { filter: "drop-shadow(0 0 10px rgba(255,120,30,0.65))" }
      ],
      { duration: 220, iterations: Math.ceil(dur / 220), easing: "ease-in-out" }
    );

    anim.onfinish = () => {
      spawnFireballImpact(layer, toPt.x, toPt.y);
      wob.cancel();
      ball.remove();
    };
  }

  function playActionFx(actionId, targetMobId) {
    if (!targetMobId) return;

    const playerEl = getPlayerCardEl();
    const mobEl = getMobCardElById(targetMobId);
    if (!playerEl || !mobEl) return;

    const fromPt = getElCenter(playerEl);
    const toPt = getElCenter(mobEl);

    switch (String(actionId).toLowerCase()) {
      case "shoot":
        fxShoot(toPt, fromPt, { duration: 160 });
        break;
      case "fireball":
        fxFireball(fromPt, toPt, { duration: 520 });
        break;
      default:
        fxShoot(fromPt, toPt, { duration: 140 });
        break;
    }
  }

  function onPlayerDidAction(actionId, target) {
    playActionFx(actionId, target);
  }

  window.testFx = function () {
    const t = getActionTarget();
    console.log("[testFx] target=", t, "playerEl=", !!getPlayerCardEl(), "mobEl=", !!getMobCardElById(t));
    playActionFx("fireball", t);
  };

  /* ======================================================================
     OLD game.js FX FUNCTIONS (player hit/heal) — moved here
     ====================================================================== */
  function playerHitEffect() {
    const anime = getAnime();
    const card =
      (window.playerDomEntry && window.playerDomEntry.card) ||
      document.querySelector(".playerAbs .mobCard");

    const wrap =
      (window.playerDomEntry && window.playerDomEntry.wrap) ||
      document.querySelector(".playerAbs");

    if (!card || !wrap || !anime) return;

    const m = (wrap.style.transform || "").match(/scale\(([^)]+)\)/);
    const scale = m ? Math.max(0.2, Number(m[1]) || 1) : 1;

    const amp = Math.round(18 / scale);

    anime.remove(card);

    animeAnimate(card, {
      translateX: [0, -amp, amp, -Math.round(amp * 0.7), Math.round(amp * 0.7), 0],
      duration: 260,
      easing: "easeOutQuad"
    });

    animeAnimate(card, {
      boxShadow: [
        "0 0 0px rgba(255,0,0,0)",
        "0 0 24px rgba(255,40,40,0.85)",
        "0 0 0px rgba(255,0,0,0)"
      ],
      duration: 320,
      easing: "easeOutQuad"
    });

    animeAnimate(card, {
      filter: ["brightness(1) saturate(1)", "brightness(1.8) saturate(1.6)", "brightness(1) saturate(1)"],
      duration: 180,
      easing: "linear"
    });
  }

  function playerHealEffect() {
    const anime = getAnime();
    const card =
      (window.playerDomEntry && window.playerDomEntry.card) ||
      document.querySelector(".playerAbs .mobCard");

    if (!card || !anime) return;

    anime.remove(card);

    animeAnimate(card, {
      scale: [1, 1.07, 1],
      duration: 320,
      easing: "easeOutQuad"
    });

    animeAnimate(card, {
      opacity: [1, 0.7, 1],
      duration: 220,
      easing: "linear"
    });
  }

  function playerHitExtremely() {
    const anime = getAnime();
    const wrap =
      (window.playerDomEntry && window.playerDomEntry.wrap) ||
      document.querySelector(".playerAbs");
    const card =
      (window.playerDomEntry && window.playerDomEntry.card) ||
      document.querySelector(".playerAbs .mobCard");

    if (!wrap || !card || !anime) {
      console.warn("[playerHitExtremely] missing wrap/card/anime", { wrap: !!wrap, card: !!card, anime: !!anime });
      return;
    }

    anime.remove([wrap, card]);

    document.body.classList.add("hitShake");
    setTimeout(() => document.body.classList.remove("hitShake"), 450);

    const flash = document.getElementById("hitFlash");
    if (flash) {
      flash.classList.remove("on");
      void flash.offsetHeight;
      flash.style.opacity = "0.65";
      flash.classList.add("on");
    }

    animeAnimate(wrap, {
      translateX: [0, -40, 40, -32, 32, -24, 24, -16, 16, 0],
      translateY: [0, 10, -10, 8, -8, 6, -6, 4, -4, 0],
      duration: 520,
      easing: "easeOutQuad"
    });

    animeAnimate(card, {
      rotate: [0, -25, 25, -18, 18, -10, 10, 0],
      scale: [1, 1.35, 0.92, 1.25, 0.98, 1.12, 1],
      duration: 520,
      easing: "easeOutElastic(1, .5)"
    });

    animeAnimate(card, {
      filter: ["brightness(1) saturate(1)", "brightness(2.6) saturate(3) contrast(1.4)", "brightness(1) saturate(1)"],
      opacity: [1, 0.55, 1, 0.7, 1],
      duration: 420,
      easing: "linear"
    });

    animeAnimate(card, {
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
    const anime = getAnime();
    const card =
      (window.playerDomEntry && window.playerDomEntry.card) ||
      document.querySelector(".playerAbs .mobCard");

    if (!card || !anime) return;

    anime.remove(card);

    animeAnimate(card, {
      boxShadow: [
        "0 0 0px rgba(0,255,120,0)",
        "0 0 45px rgba(0,255,120,0.95)",
        "0 0 0px rgba(0,255,120,0)"
      ],
      filter: ["brightness(1) saturate(1)", "brightness(2.1) saturate(2.6)", "brightness(1) saturate(1)"],
      duration: 520,
      easing: "easeOutQuad"
    });
  }

  // expose for debugging if you want
  window.playerHitEffect = playerHitEffect;
  window.playerHealEffect = playerHealEffect;
  window.playerHitExtremely = playerHitExtremely;
  window.playerHealEffectVisible = playerHealEffectVisible;

  /* ======================================================================
     XP,Level, Credits Toasts
     ====================================================================== */
  function checkPlayerStatToasts(player) {
    if (!player) return;

    if (lastPlayerStats === null) {
      lastPlayerStats = {
        credits: player.credits ?? 0,
        level: player.level ?? 0,
        xp: player.xp ?? 0,
        potions: player.potions ?? 0
      };
      return;
    }

    function diff(key, positiveMsg, negativeMsg) {
      const prev = lastPlayerStats[key];
      const curr = player[key];
      if (curr === prev) return;

      const delta = curr - prev;

      if (delta > 0) {
        if (key !== "xp") showLootToast(positiveMsg.replace("{n}", Math.abs(delta)));
        else showToast(positiveMsg.replace("{n}", Math.abs(delta)), "success", 1500);
      } else {
        showToast(negativeMsg.replace("{n}", Math.abs(delta)), "warning", 1500);
      }

      lastPlayerStats[key] = curr;
    }

    diff("credits", "+{n} Credits", "-{n} Credits");
    diff("xp", "+{n} XP", "-{n} XP");
    diff("potions", "+{n} Potion(s)", "-{n} Potion(s)");

    if (player.level !== lastPlayerStats.level) {
      if (player.level > lastPlayerStats.level) {
        showToast(`Level Up! You are now Level ${player.level}`, "success", 2200);
      } else {
        showToast(`Level changed to ${player.level}`, "warning", 1500);
      }
      lastPlayerStats.level = player.level;
    }
  }

  /* ======================================================================
     STATE POLLING
     ====================================================================== */
  async function updateState() {
    try {
      const url = new URL(STATE_URL, window.location.origin);
      const cid = getMeCharacterId();
      const gid = getGameId();
      if (cid) url.searchParams.set("characterId", cid);
      if (gid) url.searchParams.set("gameId", String(gid));

      const res = await fetch(url.toString(), { headers: authHeaders(), cache: "no-store" });
      if (res.status === 401) { doLogout(); return; }
      if (!res.ok) return;

      const s = await res.json();

      setMeFromState(s);

      updateInfoMarquee(s.infoMsg);

      const damages = detectPartyDamage(lastState || uiState, s);

      const meName = getMeName();

      if (damages.length > 0 && meName) {
        const myHit = damages.find(d => String(d.id) === meName);
        if (myHit) {
          playerHitEffect();
          playSound("hit");

          s._damagedIds = new Set([meName]);
          setTimeout(() => {
            if (uiState && uiState._damagedIds) {
              uiState._damagedIds = null;
              renderParty();
            }
          }, 450);
        }
      }

      detectMobDeaths(lastState || uiState, s);
      // ✅ keep bomb countdown aware of updated conditions (defused etc.)
      for (const mob of (s.mobs || [])) {
        const id = String(mob.id || "").trim();
        const fx = bombFxByMobId.get(id);
        if (fx) fx.mobRef = mob;
      }

      // Ranged fx on mob attacks
      if (damages.length > -10) {
        const attacker = (s.mobs || []).find(m => {
          if (Number(m.hp) <= 0) return false;
          if (isMezzed(m)) return false; // optional
          const fx = getMobEffects(m);
          return !!fx.projectile;        // only mobs configured to show an attack FX
        });

        if (attacker) handleMobAttackFx(attacker);
      }

      const keepSelectedMobId = uiState.selectedMobId;
      const keepSelectedPartyId = uiState.selectedPartyId;

      uiState = s;

      const wasGameOver = String(lastState?.phase || "") === "gameover";
      const isGameOver = String(s.phase || "") === "gameover";

      if (isGameOver) {
        showGameOverOverlay(s.gameOverReason, s.restartInMs);
        if (!wasGameOver) playSound("gameover");
      } else {
        hideGameOverOverlay();
      }

      const wasWave = String(lastState?.phase || "") === "wavecompleted";
      const isWave = String(s.phase || "") === "wavecompleted";

      if (!isGameOver && isWave) {
        if (waveOverSoundPlayed === false) {
          playSound("waveover");
          waveOverSoundPlayed = true;
        }
        showWaveOverlay(s.wave, s.restartInMs ?? null);
        if (!wasWave && typeof showToast === "function") {
          showToast(`Wave ${Number(s.wave) || 0} cleared!`, "success", 1800);
        }
      } else {
        if (waveOverSoundPlayed === true) {
          playSound("wavebegin");
          waveOverSoundPlayed = false;
        }
        hideWaveOverlay();
      }

      if (s.selectedMobId == null) uiState.selectedMobId = keepSelectedMobId;
      if (s.selectedPartyId == null) uiState.selectedPartyId = keepSelectedPartyId;

      lastState = s;

      const tmpPlayer = getMe();

      checkPlayerStatToasts(tmpPlayer);
      CheckTargetClearNeeded();
      renderTargetPillFromId(getActionTarget());

      renderAll();
    } catch (e) {
      console.error("updateState crashed:", e);
    }
  }
/* ======================================================================
     Only Way to get my character information
     ====================================================================== */
  function setMeFromState(s) {
      // pick from JSON (use whatever your server actually sends)
      ME.characterId    = String(s?.characterId    || "").trim();
      ME.characterName  = String(s?.characterName  || "").trim();
  }

  function getMeName() {
    // the ONLY displayed name
    return ME.characterName || ME.playerName || "unknown";
  }
  function getMeCharacterId() {
    return ME.characterId || "empty"; // fallback only if needed
  }

  /* ======================================================================
     Keyboard shortcuts
     ====================================================================== */
  const isDesktopInput = window.matchMedia("(pointer: fine)").matches;

  function installKeyboardShortcuts() {
    document.addEventListener("keydown", (e) => {
      if (!isDesktopInput) return;

      const tag = document.activeElement?.tagName;
      if (tag === "INPUT" || tag === "TEXTAREA") return;

      if (!/^[0-9]$/.test(e.key)) return;

      const index = e.key === "0" ? 9 : (Number(e.key) - 1);
      if (index < 0 || index >= ACTIONS.length) return;

      const actionId = ACTIONS[index].id;
      const btn = document.querySelector(`.action-btn[data-action="${actionId}"]`);
      if (!btn) return;

      e.preventDefault();
      if (btn.classList.contains("cooldown")) return;

      btn.click();
    });
  }

  /* ======================================================================
     BOOT (RELIABLE)
     ====================================================================== */
  let _started = false;
  let _stateTimer = null;
  let _cdTimer = null;

  function startApp() {
    if (_started) return;
    _started = true;

    console.log("[BOOT] startApp()");

    // required auth + character
    requireAuthOrRedirect();
    readCharacterFromUrlOnce();
    requireCharacterOrRedirect();

    // bind logout buttons
    document.getElementById("btnLogout")?.addEventListener("click", doLogout);
    document.getElementById("btnLogoutParty")?.addEventListener("click", doLogout);
    document.getElementById("btnGameOverLogout")?.addEventListener("click", doLogout);

    // bind help
    document.getElementById("btnHelp")?.addEventListener("click", openHelp);

    // resolve DOM refs now that DOM exists
    actionsEl = document.getElementById("actions");
    actionTargetEl = document.getElementById("actionTarget");
    actionMsgEl = document.getElementById("actionMsg");

    targetPillEl = document.getElementById("targetPill");
    targetPillImgEl = document.getElementById("targetPillImg");
    targetPillTextEl = document.getElementById("targetPillText");
    targetPillClearEl = document.getElementById("targetPillClear");

    // target clear button
    targetPillClearEl?.addEventListener("click", (e) => {
      e.preventDefault();
      e.stopPropagation();
      uiState.selectedMobId = null;
      clearTarget();
      updateEncounterMobs();
    });

    // Enter = Defuse
    actionMsgEl?.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        e.preventDefault();
        const talkBtn = document.querySelector('.action-btn[data-action="defuse"]');
        if (talkBtn) talkBtn.click();
      }
    });

    buildButtons();

    if (_cdTimer) clearInterval(_cdTimer);
    _cdTimer = setInterval(tickCooldowns, 100);
    tickCooldowns();

    ensureScene();
    try { updateEncounterPlayer(null); } catch (e) { console.error("updateEncounterPlayer boot failed:", e); }
    try { updateEncounterMobs(); } catch (e) { console.error("updateEncounterMobs boot failed:", e); }
    try { renderParty(); } catch (e) { console.error("renderParty boot failed:", e); }
    try { renderBadges(); } catch (e) { console.error("renderBadges boot failed:", e); }

    if (_stateTimer) clearInterval(_stateTimer);
    _stateTimer = setInterval(updateState, 555);
    updateState();

    window.addEventListener("resize", () => {
      updateEncounterMobs();
      updateEncounterPlayer(getMe());
    });

    installKeyboardShortcuts();

    // Global error handlers
    window.addEventListener("error", (e) => {
      console.error("[WINDOW ERROR]", e.message, e.filename, e.lineno, e.colno);
    });
    window.addEventListener("unhandledrejection", (e) => {
      console.error("[PROMISE REJECTION]", e.reason);
    });
  }

  window.addEventListener("DOMContentLoaded", startApp);

  window.addEventListener("pageshow", (e) => {
    console.log("[BOOT] pageshow persisted=", e.persisted);
    _started = false;
    startApp();
  });

})();
