#include "pet.h"

namespace dk {

// Zeit, bis ein Bedürfnis von voll auf leer gesunken ist (s)
static constexpr float DRAIN[Pet::N_NEEDS] = {8 * 3600.f, 6 * 3600.f, 8 * 3600.f};

void Pet::load(Store& s) {
  stage = Stage(s.get("pet_st", EGG));
  growth = s.get("pet_gp", 0);
  need[FOOD] = s.get("pet_n0", 700) / 1000.f;
  need[CUDDLE] = s.get("pet_n1", 700) / 1000.f;
  need[FUN] = s.get("pet_n2", 700) / 1000.f;
}

void Pet::save(Store& s) const {
  s.set("pet_st", stage);
  s.set("pet_gp", growth);
  s.set("pet_n0", int(need[FOOD] * 1000));
  s.set("pet_n1", int(need[CUDDLE] * 1000));
  s.set("pet_n2", int(need[FUN] * 1000));
}

void Pet::update(float dt) {
  if (stage == EGG) return;
  for (int i = 0; i < N_NEEDS; i++) {
    need[i] -= dt / DRAIN[i];
    if (need[i] < 0.05f) need[i] = 0.05f;   // ganz leer wird es nie
  }
}

bool Pet::addGrowth(int pts) {
  if (stage == EGG) return false;
  growth += pts;
  const Stage before = stage;
  if (stage == BABY && growth >= GROW_KID) stage = KID;
  if (stage == KID && growth >= GROW_BIG) stage = BIG;
  return stage != before;
}

bool Pet::applyEvents(Store& s) {
  const int play = s.get("pe_play", 0), calm = s.get("pe_calm", 0);
  if (!play && !calm) return false;
  s.set("pe_play", 0);
  s.set("pe_calm", 0);
  need[FUN] += 0.35f * play;
  need[CUDDLE] += 0.1f * calm;
  for (float& n : need) n = n > 1 ? 1 : n;
  addGrowth(play + 2 * calm);
  return true;
}

float Pet::size() const {
  switch (stage) {
    case EGG: return 44;
    case BABY: return 38;
    case KID: return 46;
    default: return 52;
  }
}

int Pet::accessory(int stars) { return stars >= 50 ? 3 : stars >= 25 ? 2 : stars >= 10 ? 1 : 0; }

}  // namespace dk
