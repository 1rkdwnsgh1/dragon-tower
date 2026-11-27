Dragon Tower | T8 1차 에셋

구성
일반 몬스터 투사체 8종 — 칸 128×128, 8프레임, 총 1024×128, 오른쪽(+X) 지향. 리치, 정예 리치, 서리 정령, 히드라, 수룡, 화염 정령, 망령, 공용 미배정 시안.
5층 고블린 족장 — 단검 세 갈래, 큰칼 휘두르기 몸 시트와 효과 시트. 단검 효과 하나를 게임에서 3방향으로 재사용한다.
10층 슬라임 왕 — 점액 웅덩이, 굴러오기 몸 시트와 효과 시트, 기존 공격 시트를 쓰는 뛰어올라 내려찍기 착지 효과. 점액 웅덩이 한 종류를 게임에서 4위치에 표시한다.
몸 시트 — 256×256 칸 12개, 발 (128,240). 현재 256px 원본 사용. 구성안의 512px 보스 몸 칸은 권장값이다.
보스 효과 — 512×512 칸 12개, 효과별 피벗은 manifest.json 참조.
PNG는 모두 가로 한 줄이며 투명 RGBA로 저장했다.

기존 에셋 연결
5층 주력 돌진/소환의 몸 동작과 10층 내려찍기의 몸 동작은 기존 공격 시트를 사용한다. 슬라임 왕 분열 및 작은 슬라임은 이전 에셋을 사용한다.
예고 원·줄·부채꼴·그림자는 엔진이 그린다. 보스 효과 스프라이트는 발사 또는 판정 시점부터 사용하는 장식이다.
manifest.json의 hit_frame은 0부터 센다. 그 전 프레임은 예고 자세이며, fps와 판정 프레임은 게임 연결 전에 조정할 수 있는 임시값이다.
투사체 표 6종과 정예 리치를 합하면 이름이 명시된 대상은 7종이다. 8번째 투사체는 assets/enemies/_unassigned/projectile-reserve/에 두었다.
실제 게임에서 에셋을 읽는 위치나 자동 배정 여부는 확인하지 않았다.

도구 및 원본
기본 이미지 생성 도구로 투사체·효과의 키 아트를 만들었다. Pillow로 프레임별 배율, 각도, 불투명도와 복귀 타이밍을 조절했다. 보스 몸은 GitHub main의 idle/attack 원본을 재배열하고 스쿼시·스트레치와 회전으로 동작을 만들었다. 굴러오기는 원래 몸 그림을 회전시킨 시안이다.
https://github.com/1rkdwnsgh1/dragon-tower/tree/main/assets/enemies

검수
17개 시트의 이름, 크기, 투명 RGBA, 칸 여백과 알파 1~16 제거를 확인했다. 화면 예고는 어느 시트에도 합성하지 않았다.
boss_skill_preview.gif는 숲 입구 배경에 보스 보조 스킬 4종을 합성한 그림이다. projectile_preview.png는 밝고 어두운 배경에서 128px 칸과 48px 축소판을 보여 준다. 게임 실행과 스킬 판정은 검증하지 않았다.

생성 프롬프트
공통 — One single isolated key sprite on truly transparent background, NO background scene, UI, border, lettering, target telegraph, shadows on floor, characters or creature parts. Painterly premium gothic dark-fantasy 2D videogame sprite, detailed tactile materials matching art reference only, crisp silhouette readable at small sprite size. Center it and leave clean margins, no separate scattering particles. Subject: 
lich — a bone arrow, aged ivory pointed bone shaft with subtle muted purple magical tail; points horizontally right
elite-lich — an elite bone arrow, longer serrated ivory bone point with deeper purple energy edges; points horizontally right
frost-spirit — a cobalt-pale blue faceted jagged ice shard with a short pale icy taper on the left; sharp tip points right
hydra — a narrow yellow olive and acid green venom spit dart, two sharp viscous edges, no bottle, thin streak trailing left, point leads right
water-dragon — a compact turquoise blue water orb with transparent curling liquid inside, bright cool cyan highlight, trailing taper left, travels right
fire-elemental — a compact rich orange and yellow fireball with rounded hot right-facing leading edge and short ragged tail to the left, no smoke
wraith — a slow spectral soul orb, dark teal and pale mint light hollow at center, tattered translucent taper left, traveling right, no face
reserve — a compact neutral amethyst arcane bolt with pointed crystalline right-facing head, short violet taper left, reusable game projectile
dagger — one bronze irregular goblin throwing dagger projectile shown horizontal, blade points right, fur-wrapped dark grip left, silhouette visible, no spinning lines
slash — a single sweeping silver-edged muted brass slash trail, bowed convex to the right, low transparent fading tail, a heavy goblin cleaver's visual swipe, no weapon
pool — one wide flattened irregular green viscous slime puddle seen from above at 35 degrees, clear sheen and dark emerald thin outline, mottled subtle inside, no droplets outside
impact — a wide low ring-like splash of green viscous goo with chunky torn edges and a dark center, 35 degree down angle, ground-contact burst only, no decals or cracks
roll — one horizontal low viscous dark green slime wake, forward rounded point right, thin broken streak left, amber-free and subdued, viewed from slightly above

