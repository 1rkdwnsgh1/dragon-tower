Dragon Tower 상점 UI

구성
assets/ui/default/ — 게임용 RGBA PNG 15개
shop_preview.png — 720×670 논리 픽셀 상점과 16px 외곽 여백. 회색 상자는 게임이 표시할 문구와 값의 자리다.
shop_icons_preview.png — 품목 7종. 어두운 배경과 밝은 배경 각각 위쪽 48px, 아래쪽 32px. 왼쪽부터 알, 소형 회복, 대형 회복, 분노, 수호, 신속, 사냥꾼의 부적.

도구와 참고
OpenAI 기본 이미지 생성 도구로 바탕과 아이콘 12종을 개별 생성했다. Pillow로 크기, 투명 여백, 가로 방향 장식 배치, 상태별 색상과 미리보기 조립을 처리했다.
GitHub main의 assets/ui/default에서 기본 버튼과 슬롯 및 탑 선택창 V2 아트를 확인했다. 탑의 어두운 보라색 바탕, 얇은 금색 테두리와 원석 포인트를 상점의 저울과 금색 버튼에 적용했다.
https://github.com/1rkdwnsgh1/dragon-tower/tree/main/assets/ui/default

맞춤과 장식 보호 여백
아래 값은 2배 해상도 원본 픽셀이다. 순서는 왼쪽 / 오른쪽 / 위 / 아래. height의 위·아래 값은 참고용이며 실제 가로 늘리기는 좌·우 값만 사용한다.
shop_panel           nine       200 / 200 / 200 / 160
shop_header          height     160 / 160 /  20 /  20
shop_row 및 hover    height     120 / 120 /  13 /  13
shop_buy 세 상태     height      60 /  60 /  16 /  16
품목 및 동전         contain       0 /   0 /   0 /   0
창틀의 상단 중앙 저울은 9조각의 상단 가운데 구간에 들어간다. 창 너비를 크게 바꾸면 해당 구간과 함께 가로로 늘어난다.

검수 및 적용 범위
15개 파일의 이름, PNG 규격, RGBA, 바깥쪽 투명도, 알파 1~16 제거를 확인했다. 행 및 버튼 상태는 원본과 알파 윤곽이 같다. 조립 및 32px·48px 미리보기에서 글자·숫자가 없고 아이콘이 구별되는지 확인했다. 초록 물약은 이미지 높이 약 65%와 84%를 사용해 작은 병과 큰 병의 차이를 냈다. 실제 게임 렌더러의 조립 및 늘리기 동작은 이번 작업 범위 밖이다.

생성 프롬프트
공통
Dragon Tower dark-fantasy 2D desktop game shop, match the reference only for style, ignore all lettering and layouts in it. Ornate aged gold #E8B85A with high #FFE3A0 and shadow #8A5A20, deep purple #1A1026 to #2A1640, subtle violet #B060FF. ZERO text, digits, labels, letters, watermarks. One isolated single asset, clean genuinely transparent exterior, no exterior shadows, mist or sparkles.

아이콘 공통
Painterly but bold simple silhouette easily readable at 32px, one object centered around 80% canvas unless specified otherwise, very thin dark outline, no pedestal.

shop_panel
A single 1440:1340 large rectangular empty shop window frame, deep very dark violet opaque center, fine metallic antique gold edge, small symmetric corner filigree within 200px corners and small gold merchant weighing scales centered at top within the top 140 pixels, top motif fully inside the frame silhouette. Straight side rails and bottom rail for nine slicing. Front on. Shape fills most canvas.

shop_header
One extremely wide low solid title band with thin gold horizontal edge lines, flat dark amethyst inner area and subtle upscale gold filigree confined to leftmost and rightmost 11% of the band; NO central ornament, entire title and amount positions empty. Front on, full-width stretchable fantasy GUI element.

shop_row
One extremely wide low solid dark purple row frame, thin aged gold straight border and minimal ornaments confined to the last 8% on each end, one circular recessed icon socket only on FAR LEFT, completely empty rest of row, designed for game inventory list. Front on, no writing.

shop_buy
A single wide gold fantasy purchase button with gentle gold bronze metallic center sufficiently calm for a runtime label, 3.9:1 width:height ratio, simple straight top and bottom subtly beveled edges continuing across middle, elaborate but compact gold pointed ornaments confined within the outer 20% either end, tiny inset purple end gems, no central motif or mark.

shop_icon_egg
A single large oval dark purple dragon egg with metallic gold patterns and a small glowing gold crack, strong shape.

shop_icon_potion_small
One SMALL short spherical glass healing bottle, luminous light emerald green liquid, narrow clean neck with dark cork. Compact rounded shape, designed to be visibly smaller than a large healing potion, object only 62% of canvas.

shop_icon_potion_large
One LARGE tall ceremonial octagonal flask with broad sloping shoulders, hefty glass base and DARK forest green magical healing liquid. Sturdy flask, prominent chunky silhouette, antique gold rim, object about 85% of canvas.

shop_icon_potion_fury
One crimson rage potion in a triangular flask whose base is broader than neck, with a molten fire swirl entirely INSIDE the bottle, no external flames or effects.

shop_icon_potion_guard
One cobalt electric blue potion in a flat broad shield-shaped glass bottle with a clear small silver shield emblem on front.

shop_icon_potion_swift
One golden yellow potion in a tall narrow teardrop glass ampoule, one bright bold lightning emblem inside the glass. Distinct tapered shape.

shop_icon_charm
One single compact hunter's charm necklace represented by a curved ivory fang, two dark purple feathers, and small purple jewel in antique gold clasp. Keep cord compact and purposefully curved above ornament. Group as one object.

icon_credit
One single face-on round antique gold coin with embossed dragon silhouette at the center. No markings around the rim except concentric metal trim.

상태 변형
shop_row_hover는 기본 줄의 금색 테두리 하이라이트를 밝게 했다. shop_buy_hover는 기본 버튼의 금색 하이라이트를 밝게 했다. shop_buy_off는 같은 버튼의 색을 어두운 회색으로 변환했다.

