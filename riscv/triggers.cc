#include "arith.h"
#include "debug_defines.h"
#include "processor.h"
#include "triggers.h"

#define ASIDMAX(SXLEN) (SXLEN == 32 ? 9 : 16)
#define SATP_ASID(SXLEN) (SXLEN == 32 ? SATP32_ASID : SATP64_ASID)
#define VMIDMAX(HSXLEN) (HSXLEN == 32 ? 7 : 14)
#define HGATP_VMID(HSXLEN) (HSXLEN == 32 ? HGATP32_VMID : HGATP64_VMID)

#define CSR_TEXTRA_MHVALUE_LENGTH(XLEN) (XLEN == 32 ? CSR_TEXTRA32_MHVALUE_LENGTH : CSR_TEXTRA64_MHVALUE_LENGTH)
#define CSR_TEXTRA_MHVALUE(XLEN)   (XLEN == 32 ? CSR_TEXTRA32_MHVALUE : CSR_TEXTRA64_MHVALUE)
#define CSR_TEXTRA_MHSELECT(XLEN)  (XLEN == 32 ? CSR_TEXTRA32_MHSELECT : CSR_TEXTRA64_MHSELECT)
#define CSR_TEXTRA_SBYTEMASK(XLEN) (XLEN == 32 ? CSR_TEXTRA32_SBYTEMASK : CSR_TEXTRA64_SBYTEMASK)
#define CSR_TEXTRA_SVALUE(XLEN)    (XLEN == 32 ? CSR_TEXTRA32_SVALUE : CSR_TEXTRA64_SVALUE)
#define CSR_TEXTRA_SSELECT(XLEN)   (XLEN == 32 ? CSR_TEXTRA32_SSELECT : CSR_TEXTRA64_SSELECT)

namespace triggers {

reg_t trigger_t::tdata2_read(const processor_t UNUSED * const proc) const noexcept {
  return tdata2;
}

void trigger_t::tdata2_write(processor_t UNUSED * const proc, const reg_t UNUSED val) noexcept {
  tdata2 = val;
}

action_t trigger_t::legalize_action(reg_t val) const noexcept {
  return (val > ACTION_MAXVAL || (val == ACTION_DEBUG_MODE && get_dmode() == 0)) ? ACTION_DEBUG_EXCEPTION : (action_t)val;
}

unsigned trigger_t::legalize_mhselect(bool h_enabled) const noexcept {
  const auto interp = interpret_mhselect(h_enabled);
  return interp.mhselect;
}

reg_t trigger_t::tdata3_read(const processor_t * const proc) const noexcept {
  auto xlen = proc->get_xlen();
  reg_t tdata3 = 0;
  tdata3 = set_field(tdata3, CSR_TEXTRA_MHVALUE(xlen), mhvalue);
  tdata3 = set_field(tdata3, CSR_TEXTRA_MHSELECT(xlen), legalize_mhselect(proc->extension_enabled('H')));
  tdata3 = set_field(tdata3, CSR_TEXTRA_SBYTEMASK(xlen), sbytemask);
  tdata3 = set_field(tdata3, CSR_TEXTRA_SVALUE(xlen), svalue);
  tdata3 = set_field(tdata3, CSR_TEXTRA_SSELECT(xlen), sselect);
  return tdata3;
}

void trigger_t::tdata3_write(processor_t * const proc, const reg_t val) noexcept {
  auto xlen = proc->get_xlen();
  mhvalue = get_field(val, CSR_TEXTRA_MHVALUE(xlen));
  mhselect = get_field(val, CSR_TEXTRA_MHSELECT(xlen));
  sbytemask = get_field(val, CSR_TEXTRA_SBYTEMASK(xlen));
  svalue = proc->extension_enabled_const('S') ? get_field(val, CSR_TEXTRA_SVALUE(xlen)) : 0;
  sselect = (sselect_t)((proc->extension_enabled_const('S') && get_field(val, CSR_TEXTRA_SSELECT(xlen)) <= SSELECT_MAXVAL) ? get_field(val, CSR_TEXTRA_SSELECT(xlen)) : SSELECT_IGNORE);
}

bool trigger_t::textra_match(processor_t * const proc) const noexcept
{
  auto xlen = proc->get_xlen();
  auto hsxlen = proc->get_xlen(); // use xlen since no hsxlen
  state_t * const state = proc->get_state();

  assert(sselect <= SSELECT_MAXVAL);
  if (sselect == SSELECT_SCONTEXT) {
    reg_t mask = (reg_t(1) << ((xlen == 32) ? CSR_TEXTRA32_SVALUE_LENGTH : CSR_TEXTRA64_SVALUE_LENGTH)) - 1;
    assert(CSR_TEXTRA32_SBYTEMASK_LENGTH < CSR_TEXTRA64_SBYTEMASK_LENGTH);
    for (int i = 0; i < CSR_TEXTRA64_SBYTEMASK_LENGTH; i++)
      if (sbytemask & (1 << i))
        mask &= 0xff << (i * 8);
    if ((state->scontext->read() & mask) != (svalue & mask))
      return false;
  } else if (sselect == SSELECT_ASID) {
    const reg_t satp = state->satp->read();
    const reg_t asid = get_field(satp,  SATP_ASID(xlen));
    if (asid != (svalue & ((1 << ASIDMAX(xlen)) - 1)))
      return false;
  }

  const auto mhselect_interp = interpret_mhselect(proc->extension_enabled('H'));
  const mhselect_mode_t mode = mhselect_interp.mode;
  if (mode == MHSELECT_MODE_MCONTEXT) { // 4, 1, and 5 are mcontext
    reg_t mask = (1 << (CSR_TEXTRA_MHVALUE_LENGTH(xlen) + 1)) - 1;
    if ((state->mcontext->read() & mask) != mhselect_interp.compare_val(mhvalue))
      return false;
  } else if (mode == MHSELECT_MODE_VMID) { // 2 and 6 are vmid
    const reg_t vmid = get_field(state->hgatp->read(), HGATP_VMID(hsxlen));
    if (vmid != (mhselect_interp.compare_val(mhvalue) & ((1 << VMIDMAX(hsxlen)) - 1)))
      return false;
  }

  return true;
}

reg_t disabled_trigger_t::tdata1_read(const processor_t * const proc) const noexcept
{
  auto xlen = proc->get_xlen();
  reg_t tdata1 = 0;
  tdata1 = set_field(tdata1, CSR_TDATA1_TYPE(xlen), CSR_TDATA1_TYPE_DISABLED);
  tdata1 = set_field(tdata1, CSR_TDATA1_DMODE(xlen), dmode);
  return tdata1;
}

void disabled_trigger_t::tdata1_write(processor_t * const proc, const reg_t val, const bool UNUSED allow_chain) noexcept
{
  // Any supported tdata.type results in disabled trigger
  auto xlen = proc->get_xlen();
  dmode = get_field(val, CSR_TDATA1_DMODE(xlen));
}

reg_t mcontrol_t::tdata1_read(const processor_t * const proc) const noexcept {
  reg_t v = 0;
  auto xlen = proc->get_xlen();
  v = set_field(v, MCONTROL_TYPE(xlen), CSR_TDATA1_TYPE_MCONTROL);
  v = set_field(v, CSR_MCONTROL_DMODE(xlen), dmode);
  v = set_field(v, MCONTROL_MASKMAX(xlen), 0);
  v = set_field(v, CSR_MCONTROL_HIT, hit);
  v = set_field(v, MCONTROL_SELECT, select);
  v = set_field(v, MCONTROL_TIMING, timing);
  v = set_field(v, MCONTROL_ACTION, action);
  v = set_field(v, MCONTROL_CHAIN, chain);
  v = set_field(v, MCONTROL_MATCH, match);
  v = set_field(v, MCONTROL_M, m);
  v = set_field(v, MCONTROL_S, s);
  v = set_field(v, MCONTROL_U, u);
  v = set_field(v, MCONTROL_EXECUTE, execute);
  v = set_field(v, MCONTROL_STORE, store);
  v = set_field(v, MCONTROL_LOAD, load);
  return v;
}

void mcontrol_t::tdata1_write(processor_t * const proc, const reg_t val, const bool allow_chain) noexcept {
  auto xlen = proc->get_xlen();
  assert(get_field(val, CSR_MCONTROL_TYPE(xlen)) == CSR_TDATA1_TYPE_MCONTROL);
  dmode = get_field(val, CSR_MCONTROL_DMODE(xlen));
  hit = get_field(val, CSR_MCONTROL_HIT);
  select = get_field(val, MCONTROL_SELECT);
  timing = get_field(val, MCONTROL_TIMING);
  action = legalize_action(get_field(val, MCONTROL_ACTION));
  chain = allow_chain ? get_field(val, MCONTROL_CHAIN) : 0;
  unsigned match_value = get_field(val, MCONTROL_MATCH);
  switch (match_value) {
    case MATCH_EQUAL:
    case MATCH_NAPOT:
    case MATCH_GE:
    case MATCH_LT:
    case MATCH_MASK_LOW:
    case MATCH_MASK_HIGH:
      match = (triggers::mcontrol_t::match_t) match_value;
      break;
    default:
      match = MATCH_EQUAL;
      break;
  }
  m = get_field(val, MCONTROL_M);
  s = proc->extension_enabled_const('S') ? get_field(val, CSR_MCONTROL_S) : 0;
  u = proc->extension_enabled_const('U') ? get_field(val, CSR_MCONTROL_U) : 0;
  execute = get_field(val, MCONTROL_EXECUTE);
  store = get_field(val, MCONTROL_STORE);
  load = get_field(val, MCONTROL_LOAD);
  // Assume we're here because of csrw.
  if (execute)
    timing = 0;
}

bool mcontrol_t::simple_match(unsigned xlen, reg_t value) const {
  switch (match) {
    case triggers::mcontrol_t::MATCH_EQUAL:
      return value == tdata2;
    case triggers::mcontrol_t::MATCH_NAPOT:
      {
        reg_t mask = ~((1 << (cto(tdata2)+1)) - 1);
        return (value & mask) == (tdata2 & mask);
      }
    case triggers::mcontrol_t::MATCH_GE:
      return value >= tdata2;
    case triggers::mcontrol_t::MATCH_LT:
      return value < tdata2;
    case triggers::mcontrol_t::MATCH_MASK_LOW:
      {
        reg_t mask = tdata2 >> (xlen/2);
        return (value & mask) == (tdata2 & mask);
      }
    case triggers::mcontrol_t::MATCH_MASK_HIGH:
      {
        reg_t mask = tdata2 >> (xlen/2);
        return ((value >> (xlen/2)) & mask) == (tdata2 & mask);
      }
  }
  assert(0);
}

std::optional<match_result_t> mcontrol_t::detect_memory_access_match(processor_t * const proc, operation_t operation, reg_t address, std::optional<reg_t> data) noexcept {
  state_t * const state = proc->get_state();
  if ((operation == triggers::OPERATION_EXECUTE && !execute) ||
      (operation == triggers::OPERATION_STORE && !store) ||
      (operation == triggers::OPERATION_LOAD && !load) ||
      (state->prv == PRV_M && !m) ||
      (state->prv == PRV_S && !s) ||
      (state->prv == PRV_U && !u) ||
      (state->v)) {
    return std::nullopt;
  }

  reg_t value;
  if (select) {
    if (!data.has_value())
      return std::nullopt;
    value = *data;
  } else {
    value = address;
  }

  // We need this because in 32-bit mode sometimes the PC bits get sign
  // extended.
  auto xlen = proc->get_xlen();
  if (xlen == 32) {
    value &= 0xffffffff;
  }

  if (simple_match(xlen, value)) {
    /* This is OK because this function is only called if the trigger was not
     * inhibited by the previous trigger in the chain. */
    hit = true;
    return match_result_t(timing_t(timing), action);
  }
  return std::nullopt;
}

reg_t itrigger_t::tdata1_read(const processor_t * const proc) const noexcept
{
  auto xlen = proc->get_xlen();
  reg_t tdata1 = 0;
  tdata1 = set_field(tdata1, CSR_ITRIGGER_TYPE(xlen), CSR_TDATA1_TYPE_ITRIGGER);
  tdata1 = set_field(tdata1, CSR_ITRIGGER_DMODE(xlen), dmode);
  tdata1 = set_field(tdata1, CSR_ITRIGGER_HIT(xlen), hit);
  tdata1 = set_field(tdata1, CSR_ITRIGGER_VS, proc->extension_enabled('H') ? vs : 0);
  tdata1 = set_field(tdata1, CSR_ITRIGGER_VU, proc->extension_enabled('H') ? vu : 0);
  tdata1 = set_field(tdata1, CSR_ITRIGGER_NMI, nmi);
  tdata1 = set_field(tdata1, CSR_ITRIGGER_M, m);
  tdata1 = set_field(tdata1, CSR_ITRIGGER_S, s);
  tdata1 = set_field(tdata1, CSR_ITRIGGER_U, u);
  tdata1 = set_field(tdata1, CSR_ITRIGGER_ACTION, action);
  return tdata1;
}

void itrigger_t::tdata1_write(processor_t * const proc, const reg_t val, const bool UNUSED allow_chain) noexcept
{
  auto xlen = proc->get_xlen();
  assert(get_field(val, CSR_ITRIGGER_TYPE(xlen)) == CSR_TDATA1_TYPE_ITRIGGER);
  dmode = get_field(val, CSR_ITRIGGER_DMODE(xlen));
  hit = get_field(val, CSR_ITRIGGER_HIT(xlen));
  vs = get_field(val, CSR_ITRIGGER_VS);
  vu = get_field(val, CSR_ITRIGGER_VU);
  nmi = get_field(val, CSR_ITRIGGER_NMI);
  m = get_field(val, CSR_ITRIGGER_M);
  s = proc->extension_enabled_const('S') ? get_field(val, CSR_ITRIGGER_S) : 0;
  u = proc->extension_enabled_const('U') ? get_field(val, CSR_ITRIGGER_U) : 0;
  action = legalize_action(get_field(val, CSR_ITRIGGER_ACTION));
}

std::optional<match_result_t> itrigger_t::detect_trap_match(processor_t * const proc, const trap_t& t) noexcept
{
  state_t * const state = proc->get_state();
  if ((state->prv == PRV_M && !m) ||
      (!state->v && state->prv == PRV_S && !s) ||
      (!state->v && state->prv == PRV_U && !u) ||
      (state->v && state->prv == PRV_S && !vs) ||
      (state->v && state->prv == PRV_U && !vu)) {
    return std::nullopt;
  }

  auto xlen = proc->get_xlen();
  bool interrupt = (t.cause() & ((reg_t)1 << (xlen - 1))) != 0;
  reg_t bit = t.cause() & ~((reg_t)1 << (xlen - 1));
  assert(bit < xlen);
  if (interrupt && ((bit == 0 && nmi) || ((tdata2 >> bit) & 1))) { // Assume NMI's exception code is 0
    hit = true;
    return match_result_t(TIMING_AFTER, action);
  }
  return std::nullopt;
}

reg_t etrigger_t::tdata1_read(const processor_t * const proc) const noexcept
{
  auto xlen = proc->get_xlen();
  reg_t tdata1 = 0;
  tdata1 = set_field(tdata1, CSR_ETRIGGER_TYPE(xlen), CSR_TDATA1_TYPE_ETRIGGER);
  tdata1 = set_field(tdata1, CSR_ETRIGGER_DMODE(xlen), dmode);
  tdata1 = set_field(tdata1, CSR_ETRIGGER_HIT(xlen), hit);
  tdata1 = set_field(tdata1, CSR_ETRIGGER_VS, proc->extension_enabled('H') ? vs : 0);
  tdata1 = set_field(tdata1, CSR_ETRIGGER_VU, proc->extension_enabled('H') ? vu : 0);
  tdata1 = set_field(tdata1, CSR_ETRIGGER_M, m);
  tdata1 = set_field(tdata1, CSR_ETRIGGER_S, s);
  tdata1 = set_field(tdata1, CSR_ETRIGGER_U, u);
  tdata1 = set_field(tdata1, CSR_ETRIGGER_ACTION, action);
  return tdata1;
}

void etrigger_t::tdata1_write(processor_t * const proc, const reg_t val, const bool UNUSED allow_chain) noexcept
{
  auto xlen = proc->get_xlen();
  assert(get_field(val, CSR_ETRIGGER_TYPE(xlen)) == CSR_TDATA1_TYPE_ETRIGGER);
  dmode = get_field(val, CSR_ETRIGGER_DMODE(xlen));
  hit = get_field(val, CSR_ETRIGGER_HIT(xlen));
  vs = get_field(val, CSR_ETRIGGER_VS);
  vu = get_field(val, CSR_ETRIGGER_VU);
  m = get_field(val, CSR_ETRIGGER_M);
  s = proc->extension_enabled_const('S') ? get_field(val, CSR_ETRIGGER_S) : 0;
  u = proc->extension_enabled_const('U') ? get_field(val, CSR_ETRIGGER_U) : 0;
  action = legalize_action(get_field(val, CSR_ETRIGGER_ACTION));
}

std::optional<match_result_t> etrigger_t::detect_trap_match(processor_t * const proc, const trap_t& t) noexcept
{
  state_t * const state = proc->get_state();
  if ((state->prv == PRV_M && !m) ||
      (!state->v && state->prv == PRV_S && !s) ||
      (!state->v && state->prv == PRV_U && !u) ||
      (state->v && state->prv == PRV_S && !vs) ||
      (state->v && state->prv == PRV_U && !vu)) {
    return std::nullopt;
  }

  auto xlen = proc->get_xlen();
  bool interrupt = (t.cause() & ((reg_t)1 << (xlen - 1))) != 0;
  reg_t bit = t.cause() & ~((reg_t)1 << (xlen - 1));
  assert(bit < xlen);
  if (!interrupt && ((tdata2 >> bit) & 1)) {
    hit = true;
    return match_result_t(TIMING_AFTER, action);
  }
  return std::nullopt;
}

module_t::module_t(unsigned count) : triggers(count) {
  for (unsigned i = 0; i < count; i++) {
    triggers[i] = new disabled_trigger_t();
  }
}

module_t::~module_t() {
  for (auto trigger : triggers) {
    delete trigger;
  }
}

reg_t module_t::tdata1_read(unsigned index) const noexcept
{
  return triggers[index]->tdata1_read(proc);
}

bool module_t::tdata1_write(unsigned index, const reg_t val) noexcept
{
  if (triggers[index]->get_dmode() && !proc->get_state()->debug_mode) {
    return false;
  }

  auto xlen = proc->get_xlen();

  // hardware should ignore writes that set dmode to 1 if the previous trigger has both dmode of 0 and chain of 1
  if (index > 0 && !triggers[index-1]->get_dmode() && triggers[index-1]->get_chain() && get_field(val, CSR_TDATA1_DMODE(xlen)))
    return false;

  unsigned type = get_field(val, CSR_TDATA1_TYPE(xlen));
  reg_t tdata1 = val;
  reg_t tdata2 = triggers[index]->tdata2_read(proc);
  reg_t tdata3 = triggers[index]->tdata3_read(proc);

  // hardware must zero chain in writes that set dmode to 0 if the next trigger has dmode of 1
  const bool allow_chain = !(index+1 < triggers.size() && triggers[index+1]->get_dmode() && !get_field(val, CSR_TDATA1_DMODE(xlen)));

  // dmode only writable from debug mode
  if (!proc->get_state()->debug_mode) {
    assert(CSR_TDATA1_DMODE(xlen) == CSR_MCONTROL_DMODE(xlen));
    assert(CSR_TDATA1_DMODE(xlen) == CSR_ITRIGGER_DMODE(xlen));
    assert(CSR_TDATA1_DMODE(xlen) == CSR_ETRIGGER_DMODE(xlen));
    tdata1 = set_field(tdata1, CSR_TDATA1_DMODE(xlen), 0);
  }

  delete triggers[index];
  switch (type) {
    case CSR_TDATA1_TYPE_MCONTROL: triggers[index] = new mcontrol_t(); break;
    case CSR_TDATA1_TYPE_ITRIGGER: triggers[index] = new itrigger_t(); break;
    case CSR_TDATA1_TYPE_ETRIGGER: triggers[index] = new etrigger_t(); break;
    default: triggers[index] = new disabled_trigger_t(); break;
  }

  triggers[index]->tdata1_write(proc, tdata1, allow_chain);
  triggers[index]->tdata2_write(proc, tdata2);
  triggers[index]->tdata3_write(proc, tdata3);
  proc->trigger_updated(triggers);
  return true;
}

reg_t module_t::tdata2_read(unsigned index) const noexcept
{
  return triggers[index]->tdata2_read(proc);
}

bool module_t::tdata2_write(unsigned index, const reg_t val) noexcept
{
  if (triggers[index]->get_dmode() && !proc->get_state()->debug_mode) {
    return false;
  }
  triggers[index]->tdata2_write(proc, val);
  proc->trigger_updated(triggers);
  return true;
}

reg_t module_t::tdata3_read(unsigned index) const noexcept
{
  return triggers[index]->tdata3_read(proc);
}

bool module_t::tdata3_write(unsigned index, const reg_t val) noexcept
{
  if (triggers[index]->get_dmode() && !proc->get_state()->debug_mode) {
    return false;
  }
  triggers[index]->tdata3_write(proc, val);
  proc->trigger_updated(triggers);
  return true;
}

std::optional<match_result_t> module_t::detect_memory_access_match(operation_t operation, reg_t address, std::optional<reg_t> data) noexcept
{
  state_t * const state = proc->get_state();
  if (state->debug_mode)
    return std::nullopt;

  bool chain_ok = true;

  for (auto trigger: triggers) {
    if (!chain_ok) {
      chain_ok = !trigger->get_chain();
      continue;
    }

    /* Note: We call detect_memory_access_match for each trigger in a chain as long as
     * the triggers are matching. This results in "temperature coding" so that
     * `hit` is set on each of the consecutive triggers that matched, even if the
     * entire chain did not match. This is allowed by the spec, because the final
     * trigger in the chain will never get `hit` set unless the entire chain
     * matches. */
    auto result = trigger->textra_match(proc) ? trigger->detect_memory_access_match(proc, operation, address, data) : std::nullopt;
    if (result.has_value() && !trigger->get_chain())
      return result;

    chain_ok = result.has_value() || !trigger->get_chain();
  }
  return std::nullopt;
}

std::optional<match_result_t> module_t::detect_trap_match(const trap_t& t) noexcept
{
  state_t * const state = proc->get_state();
  if (state->debug_mode)
    return std::nullopt;

  for (auto trigger: triggers) {
    auto result = trigger->textra_match(proc) ? trigger->detect_trap_match(proc, t) : std::nullopt;
    if (result.has_value())
      return result;
  }
  return std::nullopt;
}

reg_t module_t::tinfo_read(unsigned UNUSED index) const noexcept
{
  /* In spike, every trigger supports the same types. */
  return (1 << CSR_TDATA1_TYPE_MCONTROL) |
         (1 << CSR_TDATA1_TYPE_ITRIGGER) |
         (1 << CSR_TDATA1_TYPE_ETRIGGER) |
         (1 << CSR_TDATA1_TYPE_DISABLED);
}

};
