Dragon Tower | 탑 전투 지역 배경

구성
assets/tower/bg/에 1920×1200 불투명 RGB PNG 10장이 들어 있다.
battle_readability_preview.png는 960×600 배경 10장을 2열로 배치했다. 왼쪽 위부터 층 순서다.
각 칸에는 공격 예고 원 2개와 선 1개(#FF6040), 100px 회색 드래곤 표시 4개와 수치 자리 표시를 합성했다.

도구 및 참고
OpenAI 기본 이미지 생성 도구로 지역별 그림을 생성했다. Pillow와 NumPy로 8:5 중앙 크롭, 리사이즈, 중앙 바닥 색보정, 가독성 합성 및 수치 확인을 했다.
GitHub의 지역 구성 문서와 탑 선택창 카드·탑 일러스트, 로컬 몬스터 스프라이트를 시각 참고로 사용했다.
https://github.com/1rkdwnsgh1/dragon-tower/blob/main/docs/tower-100-bosses.md

중앙 바닥 평균 HSL 밝기
최종 PNG에서 x=288~1631, y=360~1079를 표본으로 삼았다. HSL Lightness는 각 픽셀의 (최대 RGB+최소 RGB)/2를 평균한 값이다.
zone_01.png | 1~10층 | 숲 입구 | 21.2%
zone_02.png | 11~20층 | 깊은 숲 | 20.0%
zone_03.png | 21~30층 | 설원 | 22.0%
zone_04.png | 31~40층 | 얼음 성채 | 20.3%
zone_05.png | 41~50층 | 늪지 부족 | 19.6%
zone_06.png | 51~60층 | 물가 신전 | 18.9%
zone_07.png | 61~70층 | 화산 | 18.0%
zone_08.png | 71~80층 | 망자의 성 | 17.5%
zone_09.png | 81~90층 | 마수 소굴 | 16.5%
zone_10.png | 91~100층 | 심연 | 15.8%

검수 및 범위
크기·파일명·RGB 형식·바닥 평균 밝기를 확인했다. 가독성 평가는 합성 미리보기 기준이다. 실제 게임 렌더링과 작은 창의 크롭은 실행 검증하지 않았다.
요청에 따라 이번 패키지는 10개 지역 배경을 담았다.

이미지 생성 프롬프트
공통
Single full-bleed battle background for Dragon Tower dark fantasy 2D desktop game, landscape 1920x1200 ratio 8:5. Match painterly gothic style of reference tower and cards; reference creature sprites are ONLY contextual, absolutely no creatures, people, bodies, text, numbers, UI, footprints, blood or damage indicators. Tilted 35-degree-down camera, NOT horizon/eye level. Top 20% only for distant scenery; side framing confined to outer 15% each, important landmarks at least 5% from cropped edges. Bottom 80% is one huge open nearly featureless flat walkable matte floor, with especially quiet desaturated low-contrast central 70% width. Floor brightness HSL lightness 15–25%, no large objects, no straight repetitive grid, no center path glow, no bright reflections, very fine subtle terrain variation. Soft vignette and diffuse overcast light. Red-orange #FF6040 warnings will be drawn over this game art separately, keep all orange out except minute muted accents at top outer edge of volcano. No decorative border. 

zone_01.png | 숲 입구
Forest entrance. Central floor brown-earth clearing with short sparse moss and grass; old green trees at far left/right, the gothic tower base small and far away in top 20%. Deep forest green, subdued earth brown. Slight diffuse dawn, most welcoming of set.

zone_02.png | 깊은 숲
Deep forest. Cool dark mossy soil and a sparse scatter of subdued leaf tones, unobstructed. Massive gnarled roots and trees confined to outer sides and upper band, faint pale green light filtered only in distant canopy. Deep emerald and desaturated teal.

zone_03.png | 설원
Snowfield. Open shadowed slate blue-gray compacted matte snow, flat and dark, subtle windblown texture. Snow-buried rocks and needles at edges, distant conifer silhouettes and blizzard clouds confined to upper band. Ice-gray white restricted to top.

zone_04.png | 얼음 성채
Ice citadel. Flat dark navy frozen stone floor under frosted dust, large faint irregular slabs, zero grid. At top, frozen gothic ramparts and arch with icicles; edge-only ice formations. Deep blue with restrained pale blue accents at top.

zone_05.png | 늪지 부족
별도 생성 프롬프트
Create one new opaque 8:5 game battle arena backdrop. Hand-painted gothic dark fantasy environment, cinematic matte illustration, diffuse moonless blue-gray lighting. A distinctly recognizable murky cypress swamp settlement. NO visible castle, towers, lightning, purple sky, stone buildings or distant citadel. The entire top 20 percent consists of low cypress trees standing in dull brackish water, silhouettes of tall cypress knees, uncarved trampled wooden totems/posts at both far edges, slate-green mist, hanging moss. Oblique 35 degree downward camera, with lower 80% consisting almost entirely of a huge open walkable matte muddy-brown clearing with scattered small shallow olive pools, more prominent at sides, dusty cool deep greens. Dark low saturation and low contrast 15–25% HSL lightness. Main tones distinctly olive and brown. No reflective bright surfaces. No scene object in the central 70% of this flat floor. Quiet unobstructed arena for combat overlays. No text, creatures, people, symbols, grids, blood, prints, UI or frame.

zone_06.png | 물가 신전
Waterside temple. Nearly level dark wet stone paving with subtly irregular broad slab shapes and no grid or strong specular reflections. Top: broken gothic temple pillars, water trickling down side stairs, water channels at edges only. Dark teal and moss.

zone_07.png | 화산
별도 생성 프롬프트
Original painterly gothic RPG battle backdrop, 8:5 landscape frame, no decorative border. Camera tilted 35 degrees down. The floor occupies the bottom 80% with all detailed silhouettes confined to the top 20% and outer sides. The floor is flat, continuous and dedicated to combat overlays; large clear central 70% width, desaturated, low contrast, about 17–19% HSL lightness, diffuse soft light, gentle dark vignette. No people, creatures, blood, footprints, glyphs, text, UI, grid or seam pattern. Volcano caldera battle arena. Main colors neutral graphite, charcoal, slate. Depict a clearly recognizable dark distant volcano cone or caldera rim in top band, with a couple of thin dim rusty lava channels only at extreme upper sides against cool overcast sky. No purple lightning, moon, ruins, buildings or castles. Center floor is contiguous dark flat basalt with no glow, lava or red fissures.

zone_08.png | 망자의 성
Castle of the dead. Quiet cracked dark slate graveyard flagstone floor, few subtle irregular seams. Ruined gothic castle walls and unmarked tombstone silhouettes only at outer edges and distant top. Small blue ghost lights only at the perimeter; deep muted violet and steel blue-gray.

zone_09.png | 마수 소굴
별도 생성 프롬프트
Original painterly gothic RPG battle backdrop, 8:5 landscape frame, no decorative border. Camera tilted 35 degrees down. The floor occupies the bottom 80% with all detailed silhouettes confined to the top 20% and outer sides. The floor is flat, continuous and dedicated to combat overlays; large clear central 70% width, desaturated, low contrast, about 17–19% HSL lightness, diffuse soft light, gentle dark vignette. No people, creatures, blood, footprints, glyphs, text, UI, grid or seam pattern. Deep interior of an enclosed beast lair cavern. Upper strip and side margins have coarse dark stone rock walls, subdued giant claw gouges at the outermost walls only. No visible outside, no sky, no castle, no bones. Huge open and walkable matte earthen cave floor, faint neutral umber and muted plum. Ambient desaturated purple reflected light, no bright source.

zone_10.png | 심연
별도 생성 프롬프트
Original painterly gothic RPG battle backdrop, 8:5 landscape frame, no decorative border. Camera tilted 35 degrees down. The floor occupies the bottom 80% with all detailed silhouettes confined to the top 20% and outer sides. The floor is flat, continuous and dedicated to combat overlays; large clear central 70% width, desaturated, low contrast, about 17–19% HSL lightness, diffuse soft light, gentle dark vignette. No people, creatures, blood, footprints, glyphs, text, UI, grid or seam pattern. An abyss with irregular fragments of dark levitating rock, clearly separated and suspended in an empty starless dark violet void. Floating islands occupy ONLY the thin top band and upper side margins. No tower, buildings, castle, moon, clouds or lightning. Lower 80% an immense level and subtly grained quiet black obsidian plateau with faint violet sheen, absolutely clear of crystals or cracks in center. Almost black and desaturated violet, ominous.
