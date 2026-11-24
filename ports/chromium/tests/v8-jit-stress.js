// Charge JIT soutenue pour V8 sous ALOS : assez d'iterations pour franchir
// Ignition -> Sparkplug -> Maglev/TurboFan selon les flags du shell.
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
function mix(a, i) { return (a * 31 + i) | 0; }
function hot(n) { let h = 0; for (let i = 0; i < n; i++) h = mix(h, i); return h; }
function typed(n) {
  const a = new Float64Array(1024);
  for (let r = 0; r < n; r++)
    for (let i = 0; i < a.length; i++) a[i] = a[i] * 0.5 + i;
  let s = 0; for (let i = 0; i < a.length; i++) s += a[i];
  return Math.round(s);
}
function strs(n) { let t = ''; for (let i = 0; i < n; i++) t += String.fromCharCode(97 + i % 26); return t.length; }
function objs(n) { const m = new Map(); for (let i = 0; i < n; i++) m.set('k' + (i % 512), {v: i}); return m.size; }
let sum = 0;
sum += fib(24);
for (let r = 0; r < 20; r++) sum += hot(100000);
sum += typed(200);
sum += strs(50000);
sum += objs(50000);
sum = sum | 0;
print('v8-jit-stress: PASS sum=' + sum);
