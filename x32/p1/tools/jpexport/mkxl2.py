import sys; sys.path.insert(0,'.')
from rec import *
from rend import compose
from openpyxl import Workbook
from openpyxl.drawing.image import Image as XLImage
from openpyxl.styles import Alignment, Font, PatternFill
S27=[ # id: (類別, 日文, 推定漢字/說明, 備註)
 (0,'投幣','CREDIT','',''),(1,'開始提示','1Pのスタートボタンを押して下さい','',''),
 (2,'開始提示','1Pまたは2Pのスタートボタンを押して下さい','',''),(3,'投幣','コイン　を　入れて下さい','',''),
 (4,'HUD','HI','','最高分'),(5,'HUD(1P)','VITAL／ITEM','','體力／道具'),(6,'HUD(2P)','VITAL／ITEM','',''),
 (7,'結算','戦かえど戦利金を得ず','戦えど戦利金を得ず','雖戰而未得戰利金（與 #36 相同）'),
 (8,'版權','©本宮ひろ志／®CAPCOM 集英社／© CAPCOM CO.,LTD. 1989','',''),(9,'標題','PUSH START BUTTON','',''),
 (10,'HUD(1P)','レベル／サクリャク','レベル／策略','V1.6 起顯示 LV／TAC'),(11,'HUD(1P)','パワー ▲▲▲▲','パワー','V1.6 起顯示 POW'),
 (12,'HUD(2P)','レベル／サクリャク','',''),(13,'HUD(2P)','パワー ▲▲▲▲','',''),
 (14,'章節標題','黄巾賊討伐','','第1部標題（V1.7 畫面已為漢字）'),(15,'章節標題','董卓打倒','','第2部標題'),
 (16,'關卡名','大典山','',''),(17,'關卡名','汜水関','','繁體：汜水關'),(18,'關卡名','鉄門峡','','繁體：鐵門峽'),
 (19,'關卡名','竜門谷','','繁體：龍門谷'),(20,'關卡名','虎牢関','','繁體：虎牢關'),(21,'關卡名','洛陽','',''),
 (22,'關卡名','滎陽城','',''),(23,'關卡名','郿塢城','',''),(24,'關卡地圖','ROUND','',''),(25,'結束','GAME OVER','',''),
 (26,'高分榜','五虎将軍称号の表／大将軍／前将軍／後将軍／左将軍／右将軍','','繁體：五虎將軍稱號表；將→將、軍'),
 (27,'過關結算(1P)','レベル／戦利金／得点／経験値／','','繁體：經驗值'),(28,'過關結算(2P)','レベル／戦利金／得点／経験値／','',''),
 (29,'武器名(蛇矛)','じゃほこ／ごうじゃのじゃほこ／かえんのじゃほこ／りゅうおうのだいじゃほこ','蛇矛／豪蛇の蛇矛／火炎の蛇矛／竜王の大蛇矛','即 v1 第2頁 0x2E00 一帶，此處附原字形'),
 (30,'武器名(劍)','もろはのけん／らんのつるぎ／しちせいけん／どりゅうのけん','諸刃の剣／嵐の剣／七星剣／怒竜の剣',''),
 (31,'武器名(刀)','せいりゅうとう／せいりゅうなぎなた／せきしんのげっとう／せいりゅうだいえんげつとう','青竜刀／青竜薙刀／赤心の月刀／青竜大偃月刀',''),
 (32,'武器名(槍)','しょうりゅうのやり／きじんのやり／ほうてんげき／みつくびりゅうのやり','昇竜の槍／鬼神の槍／方天戟／三首竜の槍',''),
 (33,'桃園結義','われら天に誓う。','','16x16 字型'),(34,'桃園結義','われら生れた日は違へども、','',''),
 (35,'桃園結義','死す時は、同じ日、同じ時を願わん。','',''),(36,'結算','戦かえど戦利金を得ず','戦えど戦利金を得ず',''),
 (37,'高分榜','NAME','',''),(38,'高分榜','NAME','',''),(39,'高分榜','NAME','',''),(40,'高分榜','NAME','',''),
 (41,'高分榜','LEVEL 1・2／LEVEL 3・4／LEVEL 5・6／LEVEL 7・8','',''),(42,'投幣','COIN','',''),
 (43,'投幣','ゲームをプレイするには、あと1コインひつようです','','數字由程式填入'),
 (44,'開始提示','1Pのスタートボタンを押して下さい','',''),
 (45,'開始提示','1Pのスタートボタンを押して下さい。／2Pプレイされる方は、あと1コイン入れて下さい。','',''),
 (46,'開始提示','1Pまたは2Pのスタートボタンを押して下さい','',''),(47,'投幣','FREE PLAY','',''),
]
END=[ # (record seg addr, 類別, 日文, 推定漢字, 備註)
 (0x45308,'結局（曹操獨白）','董卓め、やはりくたばりおった…','',''),(0x4532C,'結局（曹操獨白）','所詮、やつは、帝のうつわでは、','',''),
 (0x45350,'結局（曹操獨白）','なかったってことか。','',''),(0x45376,'結局（曹操獨白）','フフ…。　これでもはや中原は、','',''),
 (0x4539A,'結局（曹操獨白）','俺の手中にあるのもとうぜん。','',''),
 (0x453BC,'結局（曹操獨白）','しかし、劉備の軍ども、なかなか','',''),(0x453E0,'結局（曹操獨白）','やるわい。','',''),
 (0x453F0,'結局（曹操獨白）','天下をねらう俺にとって大きな','',''),(0x45412,'結局（曹操獨白）','わざわいとなるかもしれぬ。','',''),
 (0x45432,'結局（曹操獨白）','やつとは、いずれ、雌雄を決っす','',''),(0x45456,'結局（曹操獨白）','る時が、くるだろう。','',''),
 (0x45470,'結局（曹操獨白）','フッフフフフ','',''),
 (0x45EF4,'結局（續作預告）','天地を喰らう','',''),(0x45F06,'結局（續作預告）','第','',''),(0x45F0E,'結局（續作預告）','2','',''),
 (0x45F16,'結局（續作預告）','弾','','繁體：彈'),(0x45F1E,'結局（續作預告）','ー赤壁の戦いー','',''),
 (0x45F32,'結局（續作預告）','お楽しみに','',''),(0x45F42,'結局（續作預告）','完','',''),
 (0x458BC,'角色介紹：張飛','ちょうひ　あざなは　よくとく。ちょうはんきょうのちゅうおうに　たった','張飛　字は　翼徳。長坂橋の中央に　立った','8 像素假名，濁點在上一列'),
 (0x4594E,'角色介紹：張飛','ひとりでたち　とくいのじゃほこをふりまわし　てき　すうまんきをも','一人で立ち　得意の蛇矛を振り回し　敵　数万騎をも',''),
 (0x459D4,'角色介紹：張飛','けちらした。ちからづよさでは　かんうよりうえとされている。','蹴散らした。力強さでは　関羽より上とされている。',''),
 (0x45A4E,'角色介紹：劉備','りゅうび　あざなは　げんとく。ぜんかんのけいていのちをひき','劉備　字は　玄徳。前漢の景帝の血をひき',''),
 (0x45AD0,'角色介紹：劉備','こうきんぞくのらんにさいし　こころざしをおなじにする','黄巾賊の乱に際し　志を同じにする',''),
 (0x45B3E,'角色介紹：劉備','かんう　ちょうひとぎきょうだいのちぎりをむすび　たちあがる。','関羽　張飛と義兄弟の契りを結び　立ち上がる。',''),
 (0x45BBC,'角色介紹：關羽','かんう　あざなは　うんちょう。なだたるゆうしょうをいっとうりょうだん','関羽　字は　雲長。名だたる勇将を一刀両断',''),
 (0x45C4E,'角色介紹：關羽','にし　ごうけつのなをほしいままにしたかんうは　また　れいせつとがくもん','にし　豪傑の名をほしいままにした関羽は　また　礼節と学問',''),
 (0x45CE0,'角色介紹：關羽','をも　かねそなえたぶしょうであり　あらゆるたたかいにぶゆうをはっきする。','をも　兼ね備えた武将であり　あらゆる戦いに武勇を発揮する。',''),
 (0x45D76,'角色介紹：趙雲','あざなは　しりゅう。てきひゃくまんのじんちゅうのなかを　むじんの','字は　子龍。敵百万の陣中の中を　無人の',''),
 (0x45E00,'角色介紹：趙雲','やを　いくがごとくかけぬけた　ちょううんは　かれいで　みのこなしも','野を　行くが如く駆け抜けた　趙雲は　華麗で　身のこなしも',''),
 (0x45E8A,'角色介紹：趙雲','はやく　また　きじんもおそれぬ　しんのぶしである。','早く　また　鬼神も恐れぬ　真の武士である。',''),
]
def save(arr, fn, k=2):
    im=toimg(arr) if isinstance(arr,np.ndarray) else arr
    im=im.resize((im.width*k,im.height*k),Image.NEAREST); im.save(fn); return im
def seg_by_addr(a):
    for base in (0x45308,0x453bc,0x45ef4,0x458bc,0x45a4e,0x45bbc,0x45d76):
        segs,e=rec_segs(base)
        for s in segs:
            if s[0]==a: return s
    raise KeyError(hex(a))
def kana_img(ws):
    top=ws[0::2]; bot=ws[1::2]; im=np.full((16,8*len(bot)),15,np.uint8)
    for k,(t,b) in enumerate(zip(top,bot)):
        im[:8,k*8:k*8+8]=tile(0x6000+t,k&1); im[8:,k*8:k*8+8]=tile(0x6000+b,k&1)
    return im
wb=Workbook()
def sheet(ws,hdr,widths):
    ws.append(hdr)
    for c in ws[1]: c.font=Font(bold=True); c.fill=PatternFill('solid',fgColor='DDDDDD'); c.alignment=Alignment(vertical='center',wrap_text=True)
    for col,wd in zip('ABCDEFGHI',widths): ws.column_dimensions[col].width=wd
    ws.freeze_panes='A2'
def put_img(ws,im,cell,row,maxh=None):
    fn='img/%s.png'%cell.replace(':','_')+'_%d'%row
    fn='img/%s_%d.png'%(ws.title.encode().hex()[:8],row)
    im.save(fn); xi=XLImage(fn); ws.add_image(xi,'%s%d'%(cell,row))
    ws.row_dimensions[row].height=max(20,im.height*0.75+4)
ws=wb.active; ws.title='畫面字串(0x27A4表)'
sheet(ws,['編號','ROM位址','類別','日文（轉錄）','推定漢字','中文翻譯','備註','原始字形（像素，放大2倍）'],(6,10,14,46,30,40,30,90))
for i,cat,jp,kj,nt in S27:
    a=0x27a4+w16(0x27a4+2*i); segs=S[i][2]
    pages=sorted(set(pg for _,_,_,pg,_ in segs))
    font='16x16' if all(p>=2 for p in pages) else '8x8' if all(p<2 for p in pages) else '混合'
    ws.append([i,'0x%05X'%a,cat,jp,kj,'',('；'.join(x for x in (nt,'字型 '+font) if x))])
    r=ws.max_row
    im=compose(segs); k=2 if im.width<=420 else 1
    put_img(ws,im.resize((im.width*k,im.height*k),Image.NEAREST),'H',r)
    for col in 'DEFG': ws['%s%d'%(col,r)].alignment=Alignment(wrap_text=True,vertical='center')
ws=wb.create_sheet('結局與角色介紹')
sheet(ws,['編號','ROM位址','類別','日文（轉錄）','推定漢字','中文翻譯','備註','原始字形（像素，放大2倍）'],(6,10,16,50,40,40,24,110))
for n,(a,cat,jp,kj,nt) in enumerate(END,1):
    s0,x,y,at,wl=seg_by_addr(a)
    kana=cat.startswith('角色')
    im=toimg(kana_img(wl) if kana else rec_img(wl))
    k=2 if im.width<=500 else 1
    note=nt or ('16x16 字型（訊息紀錄表 0x45270）' if not kana else '')
    ws.append([n,'0x%05X'%a,cat,jp,kj,'',note]); r=ws.max_row
    put_img(ws,im.resize((im.width*k,im.height*k),Image.NEAREST),'H',r)
    for col in 'DEFG': ws['%s%d'%(col,r)].alignment=Alignment(wrap_text=True,vertical='center')
ws=wb.create_sheet('說明')
for t in ['天地を喰らう（CPS1 日版 dynwarj）遊戲內日文資料 第2批（v1 未收錄的部分），供中文化使用。',
          '「畫面字串(0x27A4表)」：程式 0x2670 常式依編號畫出的字串（投幣／開始提示、關卡名、章節標題、高分榜、過關結算、武器名、桃園結義誓詞等）。',
          '　格式：每段 x、y、屬性、字頁，接著字元碼；字頁 0/1 為 8x8 字型，2 以上為 16x16（每字 2 欄 2 列）。原始字形欄為依座標排好的實際畫面字形。',
          '「結局與角色介紹」：訊息紀錄表 0x45270 中 v1 未匯出的部分 —— 結局曹操獨白、續作預告，以及角色介紹（8 像素假名，濁點在上一列）。',
          '中文翻譯欄請填入；英數字串（CREDIT、PUSH START BUTTON 等）可保留原文。關卡名、章節標題原本就是漢字，填繁體寫法即可。',
          '原始字形欄的圖為 ROM 中字形的實際像素（灰階顯示，白色為透明），已放大 2 倍（過寬者 1 倍）。']:
    ws.append([t])
ws.column_dimensions['A'].width=140


import pickle
pro=pickle.load(open('pro.pkl','rb'))
PRO=['権力が、ひとつの下に／永く集中すれば、／かならず／分れる。',
     'しかし、分れて／乱れた権力は、／また　かならず／統一される。',
     '漢の高祖　劉邦が、／きずいた漢帝国も／四百年をすぎ']
ws=wb.create_sheet('開場劇情',1)
sheet(ws,['編號','ROM位址','類別','日文（轉錄，／＝換欄）','推定漢字','中文翻譯','備註','原始字形（像素，放大2倍）'],(6,10,14,40,20,40,30,70))
for n,((base,cols),jp) in enumerate(zip(pro,PRO),1):
    C=np.full((32*8,64*8),15,np.uint8); xs=[];ys=[]
    for ad,wl in cols:
        o=ad-0x900000; col=o>>7; row=(o>>2)&31
        for k,v in enumerate(wl):
            g=glyph16(0x6000+v); y=(row+2*k)*8; x=col*8
            C[y:y+16,x:x+16]=np.where(g!=15,g,C[y:y+16,x:x+16]); xs+=[x,x+16]; ys+=[y,y+16]
    im=toimg(C[min(ys):max(ys),min(xs):max(xs)])
    ws.append([n,'0x%05X'%base,'開場（直書）',jp,'','','直書，由右至左；16x16 字型（0x20B6 常式，表 0x2104）']); r=ws.max_row
    put_img(ws,im.resize((im.width*2,im.height*2),Image.NEAREST),'H',r)
    for col in 'DEFG': ws['%s%d'%(col,r)].alignment=Alignment(wrap_text=True,vertical='center')
ws.append([4,'（圖形）','開場（紅色大字）','ついに／乱れる／時が／やって／きた','ついに乱れる時がやってきた','','直書，由右至左；每字 32x24 的圖形字（scroll1 0x6700 起），不是字型，中文化需重畫圖']); r=ws.max_row
im=Image.open('red.png'); put_img(ws,im,'H',r)
for col in 'DEFG': ws['%s%d'%(col,r)].alignment=Alignment(wrap_text=True,vertical='center')
ws=wb['說明']
ws.append(['「開場劇情」：標題前的開場直書文字 3 頁（程式 0x20B6 常式、表 0x2104：每欄 長字位址＋字碼−0x6000，負值換欄）及紅色大字 1 頁（圖形字，截自街機畫面）。'])
wb.save('DynastyWars_日文文字_v2.xlsx'); print('ok')
