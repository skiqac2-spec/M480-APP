"""Monster catalogue: 51 species + 5 act bosses (order = art id used by the game)."""
from render import C
from bodies import biped, quadruped, arachnid, flyer, serpent, wraith, blob, golem

BIP = (104, 104, 52, 100)      # canvas W, H, anchor x, ground y
WING = (156, 112, 78, 108)
QUAD = (124, 92, 62, 88)
ARA = (132, 84, 66, 80)
FLY = (136, 96, 68, 92)
SERP = (104, 112, 52, 108)
WRA = (104, 112, 52, 108)
BLOB = (132, 104, 66, 100)
GOL = (132, 124, 66, 120)

S = []
def add(key, fn, p, cv, yaw=0.0):
    S.append((key, fn, p, cv, yaw))

# ---- walking dead
add('zombie', biped, dict(H=94, skin='rot', ribs=True, arms_raised=True, hunch=0.2, gut=True, armless='L',
                         wounds=[('belly', 0.06, 0.03, 0.022)], blood_mouth=True, eyec=C(220, 200, 80)), BIP, 0.32)
add('ghoul', biped, dict(H=90, skin='rot', sp={'col': C(150, 148, 138)}, hunch=0.45, long_arms=True, claws=True,
                        blood_mouth=True, build=0.85, bloody_hands=True, eyec=C(255, 255, 230), ribs=True), BIP, 0.32)
add('headless', biped, dict(H=92, skin='rot', sp={'col': C(130, 118, 98)}, headless=True, weapon='axe',
                           wounds=[('chest', -0.04, 0.0, 0.025), ('belly', 0.05, -0.02, 0.02)], gut=True), BIP, 0.32)
add('bloated', biped, dict(H=92, skin='rot', sp={'col': C(120, 132, 88)}, fat=True, gut=True, blood_mouth=True,
                          wounds=[('chest', 0.05, 0.02, 0.02), ('belly', -0.08, 0.0, 0.025)], eyec=C(230, 220, 90)), BIP, 0.32)
add('burning_dead', biped, dict(H=94, skin='skin', sp={'col': C(46, 34, 28), 'lava': True}, ribs=True, fire=1.0,
                               hunch=0.2, arms_raised=True, eyec=C(255, 160, 40)), BIP, 0.32)
# ---- skeletons
add('skeleton', biped, dict(H=96, skeleton=True, skull=True, weapon='sword', shield=True), BIP, 0.32)
add('skel_spear', biped, dict(H=96, skeleton=True, skull=True, weapon='spear', eyec=C(90, 160, 255), hunch=0.05), BIP, 0.32)
add('skel_knight', biped, dict(H=98, skeleton=True, skull=True, armor=True, armorp={'col': C(80, 76, 72)}, helm=True,
                               helmp={'col': C(80, 76, 72)}, weapon='greatsword'), BIP, 0.32)
add('ancient_skel', biped, dict(H=98, skeleton=True, skull=True, bp={'col': C(176, 198, 214)}, crown=True,
                                weapon='greatsword', wp={'col': C(160, 200, 230)}, eyec=C(80, 220, 255)), BIP, 0.32)
add('temple_guard', biped, dict(H=96, skeleton=True, skull=True, armor=True, armorp={'col': C(60, 110, 80), 'rust': 0.7},
                                helm=True, helmp={'col': C(60, 110, 80)}, weapon='spear', eyec=C(80, 255, 120)), BIP, 0.32)
# ---- demons / cultists
add('fallen', biped, dict(H=62, skin='skin', sp={'col': C(150, 42, 32)}, horns=0.8, ears=True, claws_feet=True,
                          weapon='scythe', hunch=0.3, pecs=True, tail=True, eyec=C(255, 220, 40)), BIP, 0.32)
add('shaman', biped, dict(H=64, skin='skin', sp={'col': C(110, 40, 110)}, ears=True, feathers=True, weapon='staff',
                          hunch=0.35, claws_feet=True, tail=True, eyec=C(255, 120, 255)), BIP, 0.32)
add('swamp_shaman', biped, dict(H=74, skin='skin', sp={'col': C(64, 110, 70)}, mask=True, feathers=True,
                                weapon='staff', hunch=0.3, eyec=C(120, 255, 120)), BIP, 0.32)
add('cultist', biped, dict(H=94, skin='skin', sp={'col': C(170, 140, 120)}, robe=True, robep={'col': C(46, 10, 14)},
                           hood=True, weapon='dagger', bloody_hands=True, eyec=C(255, 50, 30)), BIP, 0.32)
add('cultist_serpent', biped, dict(H=94, skin='skin', sp={'col': C(140, 150, 110)}, robe=True,
                                   robep={'col': C(30, 52, 40)}, hood=True, weapon='scythe', eyec=C(120, 255, 80)), BIP, 0.32)
add('butcher', biped, dict(H=100, skin='skin', sp={'col': C(176, 132, 116)}, fat=True, apron=True, weapon='cleaver',
                           build=1.25, bloody_hands=True, blood_mouth=True, eyec=C(255, 60, 30), headr=1.0), BIP, 0.32)
add('mummy', biped, dict(H=94, skin='rag', rags=True, hunch=0.1, arms_raised=True, eyec=C(255, 220, 90)), BIP, 0.32)
add('mummy_lord', biped, dict(H=98, skin='rag', sp={'col': C(120, 100, 60)}, mask=True, weapon='crook', robe=True,
                              robep={'col': C(60, 46, 20)}, rags=True, eyec=C(120, 255, 220)), BIP, 0.32)
add('shadow_demon', biped, dict(H=100, skin='skin', sp={'col': C(52, 42, 62)}, horns=1.2, wings={'col': C(46, 32, 56)},
                                claws=True, pecs=True, build=1.1, claws_feet=True, tail=True, eyec=C(255, 40, 200)), WING, 0.25)
add('fire_demon', biped, dict(H=100, skin='skin', sp={'col': C(110, 36, 20), 'lava': True}, horns=1.0, pecs=True,
                              fire=0.7, claws=True, tail=True, build=1.1, eyec=C(255, 230, 60)), BIP, 0.32)
add('hell_knight', biped, dict(H=100, skin='skin', sp={'col': C(70, 20, 20)}, armor=True,
                               armorp={'col': C(46, 40, 42), 'rust': 0.8, 'glow': C(255, 40, 10)}, helm=True,
                               helmp={'col': C(46, 40, 42), 'rust': 0.8}, horns=0.9, weapon='greatsword',
                               eyec=C(255, 40, 20), build=1.1), BIP, 0.32)
add('gargoyle', biped, dict(H=88, skin='stone', sp={'col': C(96, 94, 100)}, wings={'col': C(84, 82, 90)}, horns=1.0,
                            claws=True, claws_feet=True, hunch=0.35, tail=True, eyec=C(255, 90, 30)), WING, 0.25)
add('yeti', biped, dict(H=100, skin='fur', sp={'col': C(205, 205, 212)}, build=1.3, long_arms=True, claws=True,
                        hunch=0.3, blood_mouth=True, eyec=C(80, 180, 255), pecs=True), BIP, 0.32)
# ---- beasts
add('dog', quadruped, dict(L=70, h=50, sp={'col': C(104, 86, 66)}, ribs=True, wounds=[(0.15, -0.05, 0.05)]), QUAD, 0.6)
add('wolf', quadruped, dict(L=80, h=60, sp={'col': C(110, 52, 40)}, eyec=C(255, 210, 60)), QUAD, 0.6)
add('hyena', quadruped, dict(L=74, h=58, sp={'col': C(150, 120, 72)}, ribs=True, earl=1.2, eyec=C(255, 160, 40)), QUAD, 0.6)
add('hellhound', quadruped, dict(L=82, h=60, skin='skin', sp={'col': C(110, 30, 16), 'lava': True}, spikes=True,
                                 fire=True, horns=True, eyec=C(255, 230, 60)), QUAD, 0.6)
add('frost_wolf', quadruped, dict(L=84, h=62, sp={'col': C(210, 216, 226)}, eyec=C(90, 200, 255)), QUAD, 0.6)
add('rat', quadruped, dict(L=62, h=34, sp={'col': C(92, 82, 72)}, headl=0.9, earl=0.7, ribs=True,
                           wounds=[(0.1, 0.0, 0.06)], eyec=C(255, 40, 30)), QUAD, 0.6)
add('boar', quadruped, dict(L=80, h=58, sp={'col': C(80, 62, 52)}, spikes=True, horns=True, build=1.2,
                            eyec=C(255, 60, 30)), QUAD, 0.6)
# ---- arachnids / insects
add('spider', arachnid, dict(L=70, h=66, skull=True, sp={'col': C(72, 52, 82)}), ARA, 0.25)
add('spider_poison', arachnid, dict(L=72, h=66, skull=True, sp={'col': C(60, 116, 52)}, eyec=C(255, 255, 60)), ARA, 0.25)
add('spider_frost', arachnid, dict(L=72, h=66, sp={'col': C(170, 200, 222)}, eyec=C(80, 200, 255)), ARA, 0.25)
add('scarab', arachnid, dict(L=62, h=50, legs=6, shell=True, horn=True, eyes=2, sp={'col': C(40, 84, 96)},
                             eyec=C(255, 200, 60), legspan=0.8), ARA, 0.3)
add('scorpion', arachnid, dict(L=70, h=52, legs=8, scorpion=True, pincers=True, eyes=2, sp={'col': C(124, 92, 52)},
                               legspan=0.8), ARA, 0.35)
# ---- flyers
add('bat', flyer, dict(h=60, sp={'col': C(100, 78, 62)}, mem={'col': C(92, 62, 52)}), FLY)
add('bat_j', flyer, dict(h=60, sp={'col': C(62, 82, 52)}, mem={'col': C(66, 84, 52)}, ribs=True, eyec=C(255, 255, 80)), FLY)
add('hell_bat', flyer, dict(h=62, skin='skin', sp={'col': C(120, 32, 20), 'lava': True}, mem={'col': C(140, 40, 22)},
                            fire=True, eyec=C(255, 230, 60)), FLY)
# ---- serpents / worms
add('worm', serpent, dict(h=100, skin='skin', sp={'col': C(170, 132, 92), 'bloody': False}, worm=True, thick=0.12,
                          ichor=True), SERP, 0.3)
add('leech', serpent, dict(h=90, skin='skin', sp={'col': C(110, 40, 50)}, worm=True, thick=0.1), SERP, 0.3)
add('snake', serpent, dict(h=100, skin='skin', sp={'col': C(62, 92, 42), 'bloody': False}, thick=0.08), SERP, 0.3)
# ---- wraiths
add('ghost', wraith, dict(h=100, ep={'col': C(150, 190, 220)}), WRA, 0.25)
add('tortured_soul', wraith, dict(h=100, ep={'col': C(214, 112, 100), 'a': 0.7}, chains=True, eyec=C(255, 220, 120)), WRA, 0.25)
add('ice_wraith', wraith, dict(h=100, ep={'col': C(140, 212, 240)}, ribs=True, eyec=C(200, 255, 255)), WRA, 0.25)
add('wraith_lord', wraith, dict(h=104, ep={'col': C(126, 84, 176), 'a': 0.75}, crown=True, hood=True,
                                eyec=C(255, 60, 255)), WRA, 0.25)
# ---- aberrations
add('swamp_horror', blob, dict(h=88, w=100, skin='rot', sp={'col': C(80, 102, 60)}, tent=6, iris=C(220, 200, 40), seed=11), BLOB)
add('sewer_flesh', blob, dict(h=86, w=104, skin='gut', ribs=True, tent=5, seed=5, neyes=5), BLOB)
add('horror', blob, dict(h=94, w=108, skin='raw', tent=8, iris=C(255, 210, 40), neyes=16, bones=7, seed=21), BLOB)
# ---- golems
add('blood_golem', golem, dict(H=104, skin='raw', alt='gut', stitches=True, bonespikes=4, eyec=C(255, 220, 60)), GOL, 0.3)
add('stone_golem', golem, dict(H=106, skin='stone', sp={'col': C(112, 108, 102), 'glow': C(120, 160, 255)},
                               rune=C(120, 170, 255), eyec=C(120, 180, 255)), GOL, 0.3)
add('ice_golem', golem, dict(H=106, skin='ice', spikes=True, eyec=C(150, 230, 255)), GOL, 0.3)
# ---- act bosses
add('boss_vargas', biped, dict(H=126, skin='skin', sp={'col': C(140, 26, 20)}, horns=1.6, wings={'col': C(96, 22, 20)},
                               wingsize=1.1, claws=True, pecs=True, build=1.2, tail=True, claws_feet=True,
                               blood_mouth=True, eyec=C(255, 230, 60), bloody_hands=True), (196, 140, 98, 136), 0.2)
add('boss_seket', arachnid, dict(L=112, h=82, skull=True, sp={'col': C(196, 156, 72)}, legspan=1.05,
                                 eyec=C(255, 60, 30)), (196, 124, 98, 118), 0.25)
add('boss_mordek', biped, dict(H=128, skeleton=True, skull=True, crown=True, robe=True, robep={'col': C(96, 10, 10)},
                               weapon='greatsword', armor=True, armorp={'col': C(150, 118, 52), 'rust': 0.75},
                               eyec=C(255, 220, 60)), (150, 140, 75, 136), 0.3)
add('boss_dread', blob, dict(h=112, w=132, skin='raw', tent=9, heads=[(-0.28, 0), (0.0, 0), (0.28, 0)], ribs=True,
                             iris=C(255, 220, 40)), (196, 156, 98, 150))
add('boss_warden', golem, dict(H=130, seed=19, skin='stone', sp={'col': C(72, 52, 104), 'glow': C(200, 100, 255)},
                               rune=C(220, 120, 255), spikes=True, eyec=C(230, 120, 255)), (170, 150, 85, 146), 0.3)

# ---- the player character, seen from behind (32 frames: + 8 casting frames)
import math as _m
add('hero', biped, dict(H=100, skin='skin', sp={'col': C(176, 140, 116), 'bloody': False}, armor=True,
                        armorp={'col': C(120, 116, 112), 'rust': 0.75, 'bloody': False}, weapon='sword', shield=True,
                        shp={'col': C(110, 80, 50)}, shmat='wood', cape={'col': C(110, 20, 18), 'bloody': False},
                        hair={'col': C(70, 50, 34), 'bloody': False}, legmat='robe', legp={'col': C(70, 56, 44), 'bloody': False},
                        bootmat='skin', bootp={'col': C(60, 40, 26), 'bloody': False}, armmat='robe', armp={'col': C(80, 64, 50), 'bloody': False},
                        bracer='metal', bracerp={'col': C(120, 116, 112), 'rust': 0.8, 'bloody': False}, loin='rag', loinp={'col': C(90, 70, 50)}, build=1.05, hunch=0.05, eyec=C(40, 30, 30)),
    (120, 112, 60, 108), _m.pi - 0.28)
add('ally_priest', biped, dict(H=94, skin='skin', sp={'col': C(196, 160, 136), 'bloody': False}, robe=True,
                               robep={'col': C(190, 182, 160), 'bloody': False}, weapon='staff', hair={'col': C(200, 170, 90), 'bloody': False},
                               armmat='robe', armp={'col': C(190, 182, 160), 'bloody': False}, legmat='robe', legp={'col': C(160, 150, 130), 'bloody': False},
                               eyec=C(80, 160, 255), hunch=0.0), (120, 112, 60, 108), _m.pi - 0.28)
add('ally_rogue', biped, dict(H=94, skin='skin', sp={'col': C(170, 132, 104), 'bloody': False}, hood=True,
                              robep={'col': C(40, 60, 40), 'bloody': False}, cape={'col': C(40, 62, 40), 'bloody': False},
                              weapon='dagger', armmat='robe', armp={'col': C(60, 50, 40), 'bloody': False},
                              legmat='robe', legp={'col': C(50, 44, 36), 'bloody': False}, eyec=C(255, 220, 120)), (120, 112, 60, 108), _m.pi - 0.28)
add('ally_barb', biped, dict(H=102, skin='skin', sp={'col': C(186, 140, 110), 'bloody': False}, build=1.25, pecs=True,
                             weapon='axe', hair={'col': C(150, 90, 40), 'bloody': False}, legmat='fur', legp={'col': C(110, 86, 60), 'bloody': False},
                             loin='fur', loinp={'col': C(110, 86, 60)}, bracer='metal', bracerp={'col': C(110, 104, 96), 'bloody': False},
                             eyec=C(60, 40, 30)), (124, 116, 62, 112), _m.pi - 0.28)
NFRAMES = {'hero': 32, 'ally_priest': 32, 'ally_rogue': 32, 'ally_barb': 32}

KEYS = [s[0] for s in S]
