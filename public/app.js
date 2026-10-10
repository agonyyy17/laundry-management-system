const $ = (id) => document.getElementById(id);
const state = { token: sessionStorage.getItem("bw_token") || "", user: null, mode: "login", orders: [] };
const modal = $("authModal");
function api(path, options = {}) {
  const headers = { "Content-Type": "application/json", ...(options.headers || {}) };
  if (state.token) headers.Authorization = `Bearer ${state.token}`;
  return fetch(path, { ...options, headers }).then(async response => {
    const data = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(data.error || "Something went wrong. Please try again.");
    return data;
  });
}
function toast(message) {
  $("toast").textContent = message; $("toast").classList.add("show");
  setTimeout(() => $("toast").classList.remove("show"), 2600);
}
function openAuth(mode) {
  state.mode = mode; modal.classList.remove("hidden"); $("authError").textContent = "";
  $("authTitle").textContent = mode === "register" ? "Create account" : "Welcome back";
  $("authEyebrow").textContent = mode === "register" ? "START YOUR BRIGHTWASH JOURNEY" : "WELCOME TO BRIGHTWASH";
  $("authDescription").textContent = mode === "register" ? "Create an account to manage your laundry orders." : "Sign in to manage your laundry orders.";
  $("nameWrap").classList.toggle("hidden", mode !== "register");
  $("name").required = mode === "register";
  $("authSubmit").textContent = mode === "register" ? "Create account" : "Login";
  $("authSwitch").innerHTML = mode === "register" ? 'Already registered? <button class="link-button" id="switchMode">Login</button>' : 'New here? <button class="link-button" id="switchMode">Create an account</button>';
  $("switchMode").onclick = () => openAuth(mode === "register" ? "login" : "register");
  $("password").autocomplete = mode === "register" ? "new-password" : "current-password";
}
function closeAuth() { modal.classList.add("hidden"); }
["openLogin"].forEach(id => $(id).onclick = () => openAuth("login"));
["openRegister","heroRegister","ctaRegister"].forEach(id => $(id).onclick = () => openAuth("register"));
$("closeModal").onclick = closeAuth;
modal.addEventListener("click", e => { if (e.target === modal) closeAuth(); });
$("authForm").addEventListener("submit", async e => {
  e.preventDefault(); $("authError").textContent = "";
  const payload = { email: $("email").value.trim(), password: $("password").value };
  if (state.mode === "register") payload.name = $("name").value.trim();
  try {
    if (state.mode === "register") {
      await api("/api/register", { method: "POST", body: JSON.stringify(payload) });
      toast("Account created. Please log in."); openAuth("login"); $("email").value = payload.email; $("password").value = "";
    } else {
      const result = await api("/api/login", { method: "POST", body: JSON.stringify(payload) });
      state.token = result.token; state.user = result.user; sessionStorage.setItem("bw_token", state.token);
      closeAuth(); showDashboard(); await loadOrders(); toast("Welcome, " + state.user.name + "!");
    }
  } catch (err) { $("authError").textContent = err.message; }
});
function showDashboard() {
  $("dashboard").classList.remove("hidden");
  document.querySelector("main").classList.add("hidden");
  $("welcome").textContent = `Hello, ${state.user.name.split(" ")[0]}.`;
  $("roleText").textContent = state.user.role === "admin" ? "Administrator · You can manage every laundry order." : "Customer account · You can manage your own orders.";
  $("statusWrap").classList.toggle("hidden", state.user.role !== "admin");
  $("newOrder").classList.remove("hidden");
}
function showLanding() { $("dashboard").classList.add("hidden"); document.querySelector("main").classList.remove("hidden"); }
$("logout").onclick = async () => {
  try { await api("/api/logout", { method: "POST", body: "{}" }); } catch (_) {}
  state.token = ""; state.user = null; sessionStorage.removeItem("bw_token"); showLanding(); toast("You have logged out.");
};
function escapeHtml(value) { return String(value ?? "").replace(/[&<>"']/g, c => ({ "&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;" }[c])); }
async function loadOrders() {
  $("listError").textContent = "";
  try {
    const result = await api("/api/orders"); state.orders = result.orders; renderOrders();
  } catch (err) { $("listError").textContent = err.message; }
}
function renderOrders() {
  const target = $("orders");
  if (!state.orders.length) { target.innerHTML = '<div class="empty"><h3>No orders yet</h3><p class="muted">Create your first laundry order to get started.</p></div>'; return; }
  target.innerHTML = state.orders.map(o => `<article class="order-card"><div><h4>Order #${o.id} · ${escapeHtml(o.service)}</h4><div class="order-meta">${state.user.role === "admin" ? `Customer: ${escapeHtml(o.customer_name)}<br>` : ""}${Number(o.weight).toFixed(1)} kg · ${escapeHtml(o.created_at)}<br>${escapeHtml(o.notes || "No special instructions")}</div><p><span class="pill">${escapeHtml(o.status)}</span></p></div><div class="order-actions"><button data-edit="${o.id}">Edit</button><button class="delete" data-delete="${o.id}">Delete</button></div></article>`).join("");
  target.querySelectorAll("[data-edit]").forEach(b => b.onclick = () => editOrder(Number(b.dataset.edit)));
  target.querySelectorAll("[data-delete]").forEach(b => b.onclick = () => deleteOrder(Number(b.dataset.delete)));
}
function editOrder(id) {
  const order = state.orders.find(o => o.id === id); if (!order) return;
  $("orderId").value = order.id; $("service").value = order.service; $("weight").value = order.weight; $("notes").value = order.notes || "";
  $("status").value = order.status; $("orderError").textContent = ""; $("saveOrder").textContent = "Update order";
  $("orderFormWrap").classList.remove("hidden");
  if (state.user.role !== "admin") $("status").value = "Received";
  $("orderFormWrap").scrollIntoView({ behavior: "smooth", block: "center" });
}
$("newOrder").onclick = () => {
  $("orderForm").reset(); $("orderId").value = ""; $("status").value = "Received"; $("orderError").textContent = "";
  $("saveOrder").textContent = "Create order"; $("orderFormWrap").classList.remove("hidden");
};
$("cancelOrder").onclick = () => $("orderFormWrap").classList.add("hidden");
$("orderForm").addEventListener("submit", async e => {
  e.preventDefault(); $("orderError").textContent = "";
  const id = $("orderId").value;
  const payload = { service: $("service").value, weight: Number($("weight").value), notes: $("notes").value.trim(), status: state.user.role === "admin" ? $("status").value : "Received" };
  try {
    await api(id ? `/api/orders/${id}` : "/api/orders", { method: id ? "PUT" : "POST", body: JSON.stringify(payload) });
    $("orderFormWrap").classList.add("hidden"); await loadOrders(); toast(id ? "Order updated." : "Order created.");
  } catch (err) { $("orderError").textContent = err.message; }
});
async function deleteOrder(id) {
  if (!confirm(`Delete laundry order #${id}? This action cannot be undone.`)) return;
  try { await api(`/api/orders/${id}`, { method: "DELETE" }); await loadOrders(); toast("Order deleted."); }
  catch (err) { $("listError").textContent = err.message; }
}
(async function init() {
  if (!state.token) return;
  try { state.user = await api("/api/me"); showDashboard(); await loadOrders(); }
  catch (_) { state.token = ""; sessionStorage.removeItem("bw_token"); showLanding(); }
})();
