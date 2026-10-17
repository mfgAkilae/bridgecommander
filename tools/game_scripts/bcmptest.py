# bcmpfix test harness: automated multiplayer session + telemetry.
# Imported via Local.py (Autoexec does "import Local"); Tick() is called by bcmpfix (dinput.dll)
# when [Test] PythonTick is set. Inactive unless the BCMP_ROLE env var is set.
# Python 1.5 syntax (no +=, no list comprehensions, no string methods).
import App
import time
import string
import math

# The environment is read through bcmpdll (registered by the DLL before the
# first Tick), because this module is imported earlier, at Autoexec time.
g_bInit = 0
g_sRole = ''
g_sName = 'Player'
g_sJoin = '127.0.0.1'
g_iAttack = -1
g_iSpecies = 1
g_iSystem = 1
g_sAI = 'attack'
g_fLogEvery = 0.1
g_fQuitAfter = 0.0
g_bDedicated = 0
g_iMaxPlayers = 0
g_iAIShips = 0
g_sTarget = 'nearest'
g_iMission = 1
g_iTeam = 0
g_lRange = []
g_bSwitch = 0
g_bTeamSent = 0
g_fRam = 0.0
g_dNetPid = {}
g_bRamNoFire = 0
g_iRamHitAt = 0
g_fGameOverAt = 0     # Now() when the pilot saw the game end (end screen), 0 while it runs
g_fRestartAt = 0      # host: Now() when the end screen offers "Restart"
g_bRestartSent = 0
RESTART_AFTER_MS = 5000      # host: press "Restart" this long after the end screen showed it
RESTART_NO_DIALOG_MS = 20000  # host: restart this long after the game ended if no end screen shows

def Env(sKey, sDefault):
	import bcmpdll
	sVal = bcmpdll.getenv(sKey)
	if sVal is None:
		return sDefault
	return sVal

def InitEnv():
	global g_fStart, g_bInit, g_sRole, g_sName, g_sJoin, g_iSpecies, g_iSystem, g_sAI, g_fLogEvery, g_fQuitAfter
	global g_bDedicated, g_iMaxPlayers, g_iAIShips, g_sTarget, g_iMission, g_iTeam, g_lRange, g_bSwitch, g_fRam, g_bRamNoFire
	g_bInit = 1
	g_fStart = Now()
	g_sRole = Env('BCMP_ROLE', '')
	g_sName = Env('BCMP_NAME', 'Player')
	g_sJoin = Env('BCMP_JOIN', '127.0.0.1')
	g_iSpecies = string.atoi(Env('BCMP_SPECIES', '1'))
	g_iSystem = string.atoi(Env('BCMP_SYSTEM', '1'))
	g_sAI = Env('BCMP_AI', 'attack')
	g_fLogEvery = string.atof(Env('BCMP_TLOG_INTERVAL', '0.1'))
	g_fQuitAfter = string.atof(Env('BCMP_QUIT_AFTER', '0'))
	# BCMP_DEDICATED=1: the host is a dedicated server (-Dedicated), without a ship of its own
	g_bDedicated = Env('BCMP_DEDICATED', '0') == '1' and g_sRole == 'host'
	# BCMP_MAX_PLAYERS=<n>: the host raises the player limit (the menu allows 2..8)
	g_iMaxPlayers = string.atoi(Env('BCMP_MAX_PLAYERS', '0'))
	# BCMP_AI_SHIPS=<n>: the host keeps n AI ships (AI1..AIn) that attack the players; every
	# instance logs them. BCMP_TARGET=ai: the pilot only attacks AI ships.
	g_iAIShips = string.atoi(Env('BCMP_AI_SHIPS', '0'))
	g_sTarget = Env('BCMP_TARGET', 'nearest')
	# BCMP_MISSION=5: Defend the Starbase; BCMP_TEAM: 0 = attackers, 1 = defenders. With
	# BCMP_TARGET=team the pilot attacks only the other team (attackers: the starbase first).
	g_iMission = string.atoi(Env('BCMP_MISSION', '1'))
	# BCMP_RAM=<fraction>: that share of the manoeuvres is a ramming run: full impulse straight at
	# the target's centre until the ships collide (then 1.5 s more) or 12 s pass
	g_fRam = string.atof(Env('BCMP_RAM', '0'))
	g_bRamNoFire = Env('BCMP_RAM_NOFIRE', '0') == '1'
	g_iTeam = string.atoi(Env('BCMP_TEAM', '0'))
	# BCMP_RANGE=d1,d2,...: range test. The host's ship stands still and fires its phasers at a
	# stationary target ship (RT) that it moves to each distance in turn (15 s each); the
	# clients only watch.
	g_lRange = map(string.atof, filter(None, string.split(Env('BCMP_RANGE', ''), ',')))
	# BCMP_SWITCH=1: target switch test. The host's ship stands still and keeps firing at two
	# stationary ships (RT1, RT2) that it moves in turn behind it every 12 s, so its banks switch
	# from one target to the other without stopping; the clients only watch.
	g_bSwitch = Env('BCMP_SWITCH', '0') == '1'
	global g_iSeed
	if g_sRole == 'host':
		g_iSeed = string.atoi(Env('BCMP_SEED', '12345'))
	else:
		g_iSeed = string.atoi(Env('BCMP_SEED', '54321'))
	Log('# role=%s name=%s start=%.3f seed=%d' % (g_sRole, g_sName, Now(), g_iSeed))

g_sState = 'boot'
g_fStateTime = 0.0
g_fLastLog = 0.0
g_pLog = None
g_bHandlers = 0
g_iAIShipID = 0
g_fStart = 0

def MissionModule(sSuffix):
	# Multiplayer.Episode.MissionN.MissionN<suffix> of the mission under test
	sName = 'Multiplayer.Episode.Mission%d.Mission%d%s' % (g_iMission, g_iMission, sSuffix)
	exec 'import ' + sName
	import sys
	return sys.modules[sName]

def Now():
	# the telemetry clock: the same on every instance of this machine (bcmpdll.ms counts
	# from the start of this process); older DLLs only have ms
	import bcmpdll
	if hasattr(bcmpdll, 'tlogms'):
		return bcmpdll.tlogms()
	return bcmpdll.ms()

def Log(sLine):
	import bcmpdll
	bcmpdll.log(sLine)

def SetState(sState):
	global g_sState, g_fStateTime
	g_sState = sState
	g_fStateTime = Now()
	Log('X STATE %d %s' % (Now(), sState))

def InState():
	return (Now() - g_fStateTime) / 1000.0

def Tick():
	if not g_bInit:
		InitEnv()
	if not g_sRole:
		return
	try:
		DoTick()
	except:
		import sys
		Log('X ERROR %d %s %s' % (Now(), str(sys.exc_type), str(sys.exc_value)))
		pTb = sys.exc_traceback
		while pTb is not None:
			Log('X   line %d in %s' % (pTb.tb_lineno, pTb.tb_frame.f_code.co_name))
			pTb = pTb.tb_next

def DoTick():
	if g_fQuitAfter > 0 and (Now() - g_fStart) / 1000.0 > g_fQuitAfter:
		if g_sState != 'quit':
			SetState('quit')
			import bcmpdll
			bcmpdll.quit(0)
	pTop = App.TopWindow_GetTopWindow()
	if pTop is None:
		return
	if g_sState == 'boot':
		SkipIntro()
		if Env('BCMP_CMDLINE', '0') == '1':
			# the exe's -Listener/-Client options already started the network
			SetState('connecting')
		elif pTop.FindMainWindow(App.MWT_OPTIONS) is not None:
			SetState('menu')
	elif g_sState == 'menu':
		if InState() > 3.0:
			import MainMenu.mainmenu
			MainMenu.mainmenu.SwitchMiddlePane('Multiplayer')
			SetState('mpmenu')
	elif g_sState == 'mpmenu':
		if InState() > 2.0:
			StartNetwork(pTop)
			SetState('connecting')
	elif g_sState == 'connecting':
		TryStartMission()
	elif g_sState == 'ingame':
		InGame()

def StartNetwork(pTop):
	pCfg = App.g_kConfigMapping
	pCfg.SetTGStringValue('Multiplayer Options', 'Player Name', App.TGString(g_sName))
	pCfg.SetTGStringValue('Multiplayer Options', 'Game Name', App.TGString('bcmpfix-test'))
	pCfg.SetTGStringValue('Multiplayer Options', 'Password', App.TGString(''))
	pCfg.SetTGStringValue('Multiplayer Options', 'Direct Join Address', App.TGString(g_sJoin))
	pCfg.SetIntValue('Multiplayer Options', 'Internet Host', 0)
	pCfg.SetIntValue('Multiplayer Options', 'Dedicated Server', 0)
	pMultWindow = App.MultiplayerWindow_Cast(pTop.FindMainWindow(App.MWT_MULTIPLAYER))
	App.g_kUtopiaModule.SetMultiplayer(1)
	if g_sRole == 'host':
		App.g_kVarManager.SetStringVariable('Multiplayer', 'Mission', 'Multiplayer.Episode.Mission%d.Mission%d' % (g_iMission, g_iMission))
		if g_iMission != 1:
			# what clicking the game type in the host menu does (the window keeps its own choice)
			pMission = App.TGStringEvent_Create()
			pMission.SetEventType(App.ET_SET_MISSION_NAME)
			pMission.SetString('Mission%d' % g_iMission)
			pMission.SetDestination(pMultWindow)
			App.g_kEventManager.AddEvent(pMission)
		App.g_kUtopiaModule.SetIsHost(1)
		App.g_kUtopiaModule.SetIsClient(1)
		pMultWindow.SetDirectJoin(0)
	else:
		App.g_kUtopiaModule.SetIsHost(0)
		App.g_kUtopiaModule.SetIsClient(1)
		pMultWindow.SetDirectJoin(1)
	pEvent = App.TGEvent_Create()
	pEvent.SetEventType(App.ET_START)
	pEvent.SetDestination(pMultWindow)
	App.g_kEventManager.AddEvent(pEvent)
	Log('X NET %d start role=%s join=%s' % (Now(), g_sRole, g_sJoin))

def TryStartMission():
	import Multiplayer.MissionMenusShared
	import Multiplayer.MissionShared
	pMMS = Multiplayer.MissionMenusShared
	global g_fLastLog
	if Now() - g_fLastLog > 2000:
		g_fLastLog = Now()
		pNet = App.g_kUtopiaModule.GetNetwork()
		iStatus = -1
		iPlayers = -1
		if pNet:
			iStatus = pNet.GetConnectStatus()
			iPlayers = pNet.GetNumPlayers()
		Log('X CONN %d status=%d players=%d host=%d client=%d startbtn=%d' % (Now(), iStatus, iPlayers, App.g_kUtopiaModule.IsHost(), App.g_kUtopiaModule.IsClient(), pMMS.g_pStartButton is not None))
	if pMMS.g_pStartButton is None:
		return
	if InState() < 2.0:
		return
	if g_iMission == 5:
		# wait until the game has loaded the mission and run Mission5.Initialize (it creates the
		# starbase's attacker group); importing it here first would load it outside the game
		import sys
		pMod = sys.modules.get('Multiplayer.Episode.Mission5.Mission5')
		if pMod is None or pMod.g_pAttackerGroup is None:
			return
	if g_sRole == 'host':
		pMMS.SelectSystem(g_iSystem)
		if g_iMaxPlayers > 0:
			pMMS.g_iPlayerLimit = g_iMaxPlayers
			pGame = App.MultiplayerGame_Cast(App.Game_GetCurrentGame())
			if pGame:
				pGame.SetMaxPlayers(g_iMaxPlayers)
			Log('X MAXPLAYERS %d %d' % (Now(), g_iMaxPlayers))
		if g_bDedicated:
			# a dedicated server starts the game without a ship (StartMission skips UNKNOWN)
			import Multiplayer.SpeciesToShip
			MissionModule('Menus').StartMission(Multiplayer.SpeciesToShip.UNKNOWN, pMMS.g_iSystem)
			Log('X DEDICATED %d system=%d client=%d' % (Now(), pMMS.g_iSystem, App.g_kUtopiaModule.IsClient()))
			SetState('ingame')
			return
	else:
		if App.IsNull(Multiplayer.MissionShared.g_pStartingSet):
			return
	pMMS.SelectSpecies(g_iSpecies)
	SpawnShip()
	SetState('ingame')

def SendTeam():
	# Mission 5: what the mission's FinishedSelectHandler does with the team button
	global g_bTeamSent
	if g_iMission != 5:
		return
	pMenus = MissionModule('Menus')
	pMission = MissionModule('')
	pMenus.g_iTeam = g_iTeam
	pNet = App.g_kUtopiaModule.GetNetwork()
	if not pNet:
		return
	if App.g_kUtopiaModule.IsHost():
		pMission.g_kTeamDictionary[pNet.GetLocalID()] = g_iTeam
	elif not g_bTeamSent:
		pMessage = App.TGMessage_Create()
		pMessage.SetGuaranteed(1)
		kStream = App.TGBufferStream()
		kStream.OpenBuffer(256)
		kStream.WriteChar(chr(pMission.TEAM_MESSAGE))
		kStream.WriteLong(pNet.GetLocalID())
		kStream.WriteChar(chr(g_iTeam))
		pMessage.SetDataFromStream(kStream)
		pNet.SendTGMessage(pNet.GetHostID(), pMessage)
		kStream.CloseBuffer()
	g_bTeamSent = 1
	Log('X TEAM %d %d' % (Now(), g_iTeam))

def SpawnShip():
	import Multiplayer.MissionMenusShared
	pTop = App.TopWindow_GetTopWindow()
	pMultWindow = App.MultiplayerWindow_Cast(pTop.FindMainWindow(App.MWT_MULTIPLAYER))
	pMissionPane = pMultWindow.GetMissionPane()
	if pMissionPane:
		pMissionPane.SetNotVisible()
	pMMS = Multiplayer.MissionMenusShared
	SendTeam()
	MissionModule('Menus').StartMission(pMMS.g_iSpecies, pMMS.g_iSystem)
	Log('X SPAWN %d species=%d system=%d' % (Now(), pMMS.g_iSpecies, pMMS.g_iSystem))

def GetShips():
	lShips = []
	pNetwork = App.g_kUtopiaModule.GetNetwork()
	pGame = App.MultiplayerGame_Cast(App.Game_GetCurrentGame())
	if not pNetwork or not pGame:
		return lShips
	pList = pNetwork.GetPlayerList()
	i = 0
	while i < pList.GetNumPlayers():
		pPlayer = pList.GetPlayerAtIndex(i)
		pShip = pGame.GetShipFromPlayerID(pPlayer.GetNetID())
		if pShip:
			lShips.append((pPlayer.GetNetID(), pShip))
		i = i + 1
	for pShip in GetAIShips():
		lShips.append((-1, pShip))
	pStarbase = GetStarbase()
	if pStarbase:
		lShips.append((-1, pStarbase))
	return lShips

def GetStarbase():
	# Mission 5: the starbase the host creates (logged and attacked like an AI ship)
	if g_iMission != 5:
		return None
	import Multiplayer.MissionShared
	pSet = Multiplayer.MissionShared.g_pStartingSet
	if App.IsNull(pSet) or not Multiplayer.MissionShared.g_pDatabase:
		return None
	pShip = None
	if App.g_kUtopiaModule.IsHost():
		pShip = MissionModule('').g_pStarbase
	if not pShip:
		pShip = App.ShipClass_GetObject(pSet, Multiplayer.MissionShared.g_pDatabase.GetString('Starbase').GetCString())
	if pShip and not pShip.IsDead():
		global g_bStarbaseLogged
		if not g_bStarbaseLogged:
			g_bStarbaseLogged = 1
			Log('X STARBASE %d %s %d' % (Now(), pShip.GetName(), pShip.GetObjID()))
		return pShip
	return None

g_bStarbaseLogged = 0

def TeamOf(iID):
	# Mission 5: the team of a player (-1: unknown); the host passes each player's team on
	if g_iMission != 5 or iID == -1:
		return -1
	pMission = MissionModule('')
	if pMission.g_kTeamDictionary.has_key(iID):
		return pMission.g_kTeamDictionary[iID]
	return -1

def GetAIShips():
	# the AI ships (AI1..AIn) in the starting set
	lShips = []
	if g_iAIShips <= 0:
		return lShips
	import Multiplayer.MissionShared
	pSet = Multiplayer.MissionShared.g_pStartingSet
	if App.IsNull(pSet):
		return lShips
	i = 1
	while i <= g_iAIShips:
		pShip = App.ShipClass_GetObject(pSet, 'AI%d' % i)
		if pShip and not pShip.IsDead():
			lShips.append(pShip)
		i = i + 1
	return lShips

g_lAIClasses = ('Galor', 'Keldon', 'Vorcha', 'BirdOfPrey', 'Akira', 'Galaxy', 'Warbird', 'Nebula', 'Sovereign', 'Ambassador')
g_dAIGone = {}
g_fNextAICheck = 0.0

def KeepAIShips():
	# host: (re)create the AI ships; one that is gone is recreated 15 s later
	global g_fNextAICheck
	if g_iAIShips <= 0 or g_sRole != 'host' or Now() < g_fNextAICheck or InState() < 10.0:
		return
	g_fNextAICheck = Now() + 1000
	import Multiplayer.MissionShared
	import loadspacehelper
	import AI.Compound.BasicAttack
	pSet = Multiplayer.MissionShared.g_pStartingSet
	if App.IsNull(pSet):
		return
	pTargets = App.ObjectGroup()
	for sName in ('Ace', 'Bravo', 'Charlie', 'Delta', 'Echo', 'Foxtrot', 'Golf', 'Hotel', 'India', 'Juliett', 'Kilo', 'Lima'):
		pTargets.AddName(sName)
		pTargets.AddName(sName + '1')
		pTargets.AddName(sName + '2')
	i = 1
	while i <= g_iAIShips:
		sName = 'AI%d' % i
		pObj = App.ShipClass_GetObject(pSet, sName)
		if pObj:
			if g_dAIGone.has_key(sName):
				del g_dAIGone[sName]
		elif not g_dAIGone.has_key(sName):
			g_dAIGone[sName] = Now() + (i > 0 and 15000 or 0)
			if not g_dAIGone.has_key('started'):
				g_dAIGone[sName] = Now()
		elif Now() >= g_dAIGone[sName]:
			del g_dAIGone[sName]
			pShip = loadspacehelper.CreateShip(g_lAIClasses[(i - 1) % len(g_lAIClasses)], pSet, sName, '')
			if pShip:
				kP = App.TGPoint3()
				kP.SetXYZ(Rand() * 160.0 - 80.0, Rand() * 160.0 - 80.0, Rand() * 160.0 - 80.0)
				pShip.SetTranslate(kP)
				pShip.RandomOrientation()
				pShip.UpdateNodeOnly()
				pSet.GetProximityManager().UpdateObject(pShip)
				if not g_bRamNoFire:
					# (the ramming test without weapons keeps the AI ships passive: hull losses come from
					# collisions only)
					pShip.SetAI(AI.Compound.BasicAttack.CreateAI(pShip, pTargets))
				Log('X AISHIP %d %s %s' % (Now(), sName, g_lAIClasses[(i - 1) % len(g_lAIClasses)]))
		i = i + 1
	g_dAIGone['started'] = 1

# ---- scripted pilot: flies the local player ship through the same calls the
# keyboard/mouse handlers use (no AI), aggressively, to provoke desync.
g_iSeed = 12345
g_iManeuverEnd = 0
g_sManeuver = ''
g_iNextVolley = 0
g_iVolleyEnd = 0
g_iTargetID = 0
g_iTargetUntil = 0

def Rand():
	# Python 1.5 ints are 32 bit and do not promote, so use longs
	global g_iSeed
	g_iSeed = (long(g_iSeed) * 1103515245L + 12345L) % 2147483648L
	return float((g_iSeed / 65536L) % 32768L) / 32768.0

def MakePoint(fX, fY, fZ):
	kP = App.TGPoint3()
	kP.SetXYZ(fX, fY, fZ)
	return kP

def FindEnemy(pPlayer):
	# keep the current target for 20 s, then switch to the nearest enemy
	# (with one enemy this is always that ship)
	global g_iTargetUntil
	t = Now()
	kP = pPlayer.GetWorldLocation()
	pCur = None
	pBest = None
	fBest = 0.0
	for (iID, pShip) in GetShips():
		if pShip.GetObjID() == pPlayer.GetObjID() or pShip.IsDead() or pShip.IsDying():
			continue
		if g_sTarget == 'ai' and iID != -1:
			continue
		if g_sTarget == 'team':
			if iID == -1 and g_iTeam == 1:
				continue  # defenders do not attack the starbase
			if iID != -1 and TeamOf(iID) == g_iTeam:
				continue
			if iID == -1 and g_iTeam == 0 and pShip.GetObjID() != g_iTargetID:
				# attackers go for the starbase
				g_iTargetUntil = t + 20000
				return pShip
		if pShip.GetObjID() == g_iTargetID:
			pCur = pShip
		kE = pShip.GetWorldLocation()
		fDX = kE.GetX() - kP.GetX()
		fDY = kE.GetY() - kP.GetY()
		fDZ = kE.GetZ() - kP.GetZ()
		fD = fDX * fDX + fDY * fDY + fDZ * fDZ
		if pBest is None or fD < fBest:
			pBest = pShip
			fBest = fD
	if pCur is not None and t < g_iTargetUntil:
		return pCur
	g_iTargetUntil = t + 20000
	return pBest

def Pilot(pPlayer):
	global g_iManeuverEnd, g_sManeuver, g_iNextVolley, g_iVolleyEnd, g_iTargetID, g_iRamHitAt
	pEnemy = FindEnemy(pPlayer)
	t = Now()
	if pEnemy is None:
		pPlayer.SetImpulse(0.0, App.TGPoint3_GetModelForward(), App.PhysicsObjectClass.DIRECTION_MODEL_SPACE)
		return
	if g_iTargetID != pEnemy.GetObjID():
		g_iTargetID = pEnemy.GetObjID()
		pPlayer.SetAlertLevel(App.ShipClass.RED_ALERT)
		pPlayer.SetTarget(pEnemy.GetName())
		Log('X PILOT %d %s targets %s' % (t, pPlayer.GetName(), pEnemy.GetName()))
	if g_sManeuver == 'ram' and g_iRamHitAt and t - g_iRamHitAt > 1500:
		g_iManeuverEnd = t  # rammed: back to the normal manoeuvres
	if t >= g_iManeuverEnd:
		r = Rand()
		if g_fRam > 0 and Rand() < g_fRam:
			g_sManeuver = 'ram'
			g_iManeuverEnd = t + 12000
			g_iRamHitAt = 0
		elif r < 0.5:
			g_sManeuver = 'chase'
			g_iManeuverEnd = t + 3000 + int(Rand() * 3000)
		elif r < 0.85:
			g_sManeuver = 'jink'
			g_iManeuverEnd = t + 1500 + int(Rand() * 1500)
			kAng = MakePoint(Rand() * 2.0 - 1.0, Rand() * 2.0 - 1.0, Rand() * 2.0 - 1.0)
			pPlayer.SetTargetAngularVelocityDirect(kAng)
		else:
			g_sManeuver = 'brake'
			g_iManeuverEnd = t + 1000 + int(Rand() * 1000)
		Log('X MANEUVER %d %s %s' % (t, pPlayer.GetName(), g_sManeuver))
	if g_sManeuver == 'ram':
		# aim at the intercept point: the target's position plus its velocity times the time to close in
		kE = pEnemy.GetWorldLocation()
		kP = pPlayer.GetWorldLocation()
		kV = pEnemy.GetVelocityTG()
		fDist = math.sqrt((kE.GetX() - kP.GetX()) ** 2 + (kE.GetY() - kP.GetY()) ** 2 + (kE.GetZ() - kP.GetZ()) ** 2)
		kO = pPlayer.GetVelocityTG()
		fSpeed = math.sqrt(kO.GetX() * kO.GetX() + kO.GetY() * kO.GetY() + kO.GetZ() * kO.GetZ())
		fT = min(fDist / max(fSpeed, 2.0), 6.0)
		pPlayer.TurnTowardLocation(MakePoint(kE.GetX() + kV.GetX() * fT, kE.GetY() + kV.GetY() * fT, kE.GetZ() + kV.GetZ() * fT))
		pPlayer.SetImpulse(1.0, App.TGPoint3_GetModelForward(), App.PhysicsObjectClass.DIRECTION_MODEL_SPACE)
	elif g_sManeuver == 'chase':
		kE = pEnemy.GetWorldLocation()
		kOff = MakePoint(kE.GetX() + 30.0 * (Rand() - 0.5), kE.GetY() + 30.0 * (Rand() - 0.5), kE.GetZ() + 30.0 * (Rand() - 0.5))
		pPlayer.TurnTowardLocation(kOff)
		pPlayer.SetImpulse(1.0, App.TGPoint3_GetModelForward(), App.PhysicsObjectClass.DIRECTION_MODEL_SPACE)
	elif g_sManeuver == 'jink':
		pPlayer.SetImpulse(0.8, App.TGPoint3_GetModelForward(), App.PhysicsObjectClass.DIRECTION_MODEL_SPACE)
	else:
		pPlayer.SetImpulse(0.0, App.TGPoint3_GetModelForward(), App.PhysicsObjectClass.DIRECTION_MODEL_SPACE)
	# weapons: torpedo + phaser volleys like a player holding the fire buttons (not in the
	# ramming test without weapons, BCMP_RAM_NOFIRE=1: hull losses then come from collisions only)
	if g_bRamNoFire:
		pass
	elif t >= g_iNextVolley:
		g_iNextVolley = t + 1200 + int(Rand() * 1300)
		g_iVolleyEnd = t + 600
		for eGroup in (App.ShipClass.WG_PRIMARY, App.ShipClass.WG_SECONDARY):
			pSystem = pPlayer.GetWeaponSystemGroup(eGroup)
			if pSystem:
				pSystem.StartFiring(pPlayer.GetTarget(), pPlayer.GetTargetOffsetTG())
				pSystem.SetForceUpdate(1)
		Log('X FIRE %d %s' % (t, pPlayer.GetName()))
	elif g_iVolleyEnd and t >= g_iVolleyEnd:
		g_iVolleyEnd = 0
		for eGroup in (App.ShipClass.WG_PRIMARY, App.ShipClass.WG_SECONDARY):
			pSystem = pPlayer.GetWeaponSystemGroup(eGroup)
			if pSystem:
				pSystem.StopFiring()

def GameOver():
	"""1 while the game is over: from the end of the starbase cutscene or the end message
	(MissionShared.g_bGameOver) until the mission restarts. The game has deleted the ships; a
	player only sees the end screen. The host restarts like a player who presses "Restart", and
	the pilots pick team and ship again after the restart, like a player in the ship select
	screen. Spawning in the frames in which the game over deletes the ships made clients crash
	(the stock crashes behind FixRestartCrashes) and left ships without an owner on the host."""
	global g_fGameOverAt, g_fRestartAt, g_bRestartSent, g_bTeamSent
	import Multiplayer.MissionShared
	if Multiplayer.MissionShared.g_bGameOver:
		t = Now()
		if not g_fGameOverAt:
			g_fGameOverAt = t
			Log('X GAMEOVER %d' % t)
		if App.g_kUtopiaModule.IsHost() and not g_bRestartSent:
			if not g_fRestartAt and RestartOffered():
				g_fRestartAt = t
			if (g_fRestartAt and t - g_fRestartAt > RESTART_AFTER_MS) or t - g_fGameOverAt > RESTART_NO_DIALOG_MS:
				g_bRestartSent = 1
				RequestRestart()
		return 1
	if g_fGameOverAt:
		# the mission restarted (RestartGame cleared the teams): team and ship again in 8 s
		g_fGameOverAt = 0
		g_fRestartAt = 0
		g_bRestartSent = 0
		g_bTeamSent = 0
		Log('X RESTARTED %d' % Now())
		SetState('ingame')
	return 0

def RestartOffered():
	# the end screen shows the "Restart" button (host only)
	import Multiplayer.MissionMenusShared
	pButton = Multiplayer.MissionMenusShared.g_pEndRestartButton
	try:
		return pButton is not None and pButton.IsVisible()
	except:
		return 0

def RequestRestart():
	# what the end screen's "Restart" button sends
	import Multiplayer.MissionShared
	import MissionLib
	pMission = MissionLib.GetMission()
	if not pMission:
		return
	pEvent = App.TGEvent_Create()
	pEvent.SetEventType(Multiplayer.MissionShared.ET_RESTART_GAME)
	pEvent.SetDestination(pMission)
	App.g_kEventManager.AddEvent(pEvent)
	Log('X RESTART %d' % Now())

def InGame():
	global g_fLastLog, g_iAIShipID
	pGame = App.Game_GetCurrentGame()
	if not pGame:
		return
	InstallHandlers()
	AttackOnce()
	if GameOver():
		return
	pPlayer = pGame.GetPlayer()
	KeepAIShips()
	if g_bDedicated:
		# no ship: only watch and log
		pPlayer = None
		DamageEnginesOnce(None)
	elif not pPlayer or pPlayer.IsDead():
		if InState() > 8.0:
			SpawnShip()
			SetState('ingame')
		return
	else:
		if RangeTest(pPlayer) or SwitchTest(pPlayer):
			pass
		elif g_sAI == 'attack':
			Pilot(pPlayer)
		if InState() > 5.0:
			TestInfoPaneGuard()
		DamageEnginesOnce(pPlayer)
		KickShip(pPlayer)
	t = Now()
	LogSubsystems(t)
	LogBanks(t)
	if t - g_fLastLog < g_fLogEvery * 1000.0:
		return
	g_fLastLog = t
	fGameTime = App.g_kUtopiaModule.GetGameTime()
	for (iID, pShip) in GetShips():
		if not g_dNetPid.has_key(pShip.GetObjID()):
			# once per ship: the network player ID the game gives it here (0 = not a player's ship)
			g_dNetPid[pShip.GetObjID()] = 1
			Log('X NETPID %d %s %d %d' % (t, ShipName(pShip), pShip.GetObjID(), Try(lambda s=pShip: s.GetNetPlayerID(), -2)))
		kPos = pShip.GetWorldLocation()
		kVel = pShip.GetVelocityTG()
		fHull = -1.0
		pHull = pShip.GetHull()
		if pHull:
			fHull = pHull.GetCondition()
		iLocal = 0
		if pPlayer and pShip.GetObjID() == pPlayer.GetObjID():
			iLocal = 1
		elif iID == -1 and g_sRole == 'host':
			iLocal = 1  # the host simulates the AI ships
		kFwd = pShip.GetWorldForwardTG()
		Log('S %d %.3f %s %d %d %d %.2f %.2f %.2f %.2f %.2f %.2f %.1f %d %.3f %.3f %.3f' % (t, fGameTime, ShipName(pShip), iID, pShip.GetObjID(), iLocal, kPos.GetX(), kPos.GetY(), kPos.GetZ(), kVel.GetX(), kVel.GetY(), kVel.GetZ(), fHull, pShip.IsDying(), kFwd.GetX(), kFwd.GetY(), kFwd.GetZ()))
	LogTorpedoes(t, pPlayer)

def LogTorpedoes(t, pPlayer):
	# Q t torpedo-id shooter x y z: every torpedo in our set (BCMP_LOG_TORPS=1)
	if Env('BCMP_LOG_TORPS', '1') != '1':
		return
	if pPlayer is None:
		# dedicated server: the set of any ship
		lShips = GetShips()
		if not lShips:
			return
		pPlayer = lShips[0][1]
	pSet = pPlayer.GetContainingSet()
	if not pSet:
		return
	pObj = pSet.GetFirstObject()
	if App.IsNull(pObj):
		return
	iFirst = pObj.GetObjID()
	iGuard = 0
	while iGuard < 400:
		pTorp = App.Torpedo_Cast(pObj)
		if pTorp:
			kP = pTorp.GetWorldLocation()
			sShooter = '?'
			pShooter = App.ShipClass_GetObjectByID(None, pTorp.GetParentID())
			if pShooter:
				sShooter = ShipName(pShooter)
			Log('Q %d %d %s %.2f %.2f %.2f' % (t, pTorp.GetObjID(), sShooter, kP.GetX(), kP.GetY(), kP.GetZ()))
		pObj = pSet.GetNextObject(pObj.GetObjID())
		if App.IsNull(pObj) or pObj.GetObjID() == iFirst:
			break
		iGuard = iGuard + 1

g_dBankState = {}

def LogBanks(t):
	# B t ship bank on|off target: every change of a phaser bank's firing state, for all ships
	if Env('BCMP_LOG_BANKS', '1') != '1':
		return
	for (iID, pShip) in GetShips():
		pSys = pShip.GetPhaserSystem()
		if not pSys:
			continue
		i = 0
		n = pSys.GetNumChildSubsystems()
		while i < n:
			pBank = App.Weapon_Cast(pSys.GetChildSubsystem(i))
			if pBank:
				iOn = pBank.IsFiring()
				k = (pShip.GetObjID(), i)
				if g_dBankState.get(k, 0) != iOn:
					g_dBankState[k] = iOn
					Log('B %d %s %d %d %d %s' % (t, ShipName(pShip), i, iOn, pBank.GetTargetID(), string.replace(pBank.GetName(), ' ', '_')))
			i = i + 1

g_iNextSubLog = 0

def Cond(pSub):
	# condition of a subsystem as a fraction of its maximum, -1 if missing
	if not pSub:
		return -1.0
	try:
		return pSub.GetConditionPercentage()
	except:
		return -1.0

def LogSubsystems(t):
	# once a second per ship: shield facings and subsystem conditions as this instance sees them
	# P t name sh0 .. sh5 impulse phaser torpedo pulse power sensor
	global g_iNextSubLog
	if t < g_iNextSubLog:
		return
	g_iNextSubLog = t + 1000
	for (iID, pShip) in GetShips():
		lVals = []
		pShields = pShip.GetShields()
		i = 0
		while i < 6:
			fV = -1.0
			if pShields:
				try:
					fV = pShields.GetCurShields(i)
				except:
					fV = -1.0
			lVals.append('%.1f' % fV)
			i = i + 1
		for pSub in (pShip.GetImpulseEngineSubsystem(), pShip.GetPhaserSystem(), pShip.GetTorpedoSystem(),
				pShip.GetPulseWeaponSystem(), pShip.GetPowerSubsystem(), pShip.GetSensorSubsystem()):
			lVals.append('%.3f' % Cond(pSub))
		Log('P %d %s %s' % (t, ShipName(pShip), string.join(lVals, ' ')))

def InstallHandlers():
	global g_bHandlers
	if g_bHandlers:
		return
	import MissionLib
	pMission = MissionLib.GetMission()
	if not pMission:
		return
	App.g_kEventManager.AddBroadcastPythonFuncHandler(App.ET_WEAPON_HIT, pMission, __name__ + '.OnWeaponHit')
	App.g_kEventManager.AddBroadcastPythonFuncHandler(App.ET_OBJECT_EXPLODING, pMission, __name__ + '.OnExploding')
	App.g_kEventManager.AddBroadcastPythonFuncHandler(App.ET_TORPEDO_FIRED, pMission, __name__ + '.OnTorpedoFired')
	App.g_kEventManager.AddBroadcastPythonFuncHandler(App.ET_OBJECT_COLLISION, pMission, __name__ + '.OnCollision')
	App.g_kEventManager.AddBroadcastPythonFuncHandler(App.ET_HOST_OBJECT_COLLISION, pMission, __name__ + '.OnHostCollision')
	g_bHandlers = 1
	Log('X HANDLERS %d' % Now())

def ShipName(pShip):
	# one field in the telemetry lines
	return string.replace(pShip.GetName(), ' ', '_')

def ObjName(pObj):
	if not pObj:
		return '-'
	try:
		return ShipName(App.ObjectClass_Cast(pObj))
	except:
		return '?'

def Try(pFunc, sDefault):
	try:
		return pFunc()
	except:
		return sDefault

def SourceShipName(pSource):
	# the ship that fired: a torpedo's parent, or the ship of a phaser bank
	pTorp = App.Torpedo_Cast(pSource)
	if pTorp:
		pShip = App.ShipClass_GetObjectByID(None, pTorp.GetParentID())
		if pShip:
			return ShipName(pShip)
		return '?'
	pSub = App.ShipSubsystem_Cast(pSource)
	if pSub and pSub.GetParentShip():
		return ShipName(pSub.GetParentShip())
	return ObjName(pSource)

def OnWeaponHit(pObject, pEvent):
	t = Now()
	sTarget = Try(lambda e=pEvent: ObjName(e.GetDestination()), '?')
	sFp = Try(lambda e=pEvent: '%d' % e.GetFiringPlayerID(), '?')
	sDmg = Try(lambda e=pEvent: '%.1f' % e.GetDamage(), '?')
	sType = Try(lambda e=pEvent: '%d' % e.GetWeaponType(), '?')
	sHull = Try(lambda e=pEvent: '%d' % e.IsHullHit(), '?')
	sWid = Try(lambda e=pEvent: '%d' % e.GetWeaponInstanceID(), '?')
	sAt = Try(lambda e=pEvent: '%.1f,%.1f,%.1f' % (e.GetWorldHitPoint().GetX(), e.GetWorldHitPoint().GetY(), e.GetWorldHitPoint().GetZ()), '?')
	sSrc = Try(lambda e=pEvent: SourceShipName(e.GetSource()), '?')
	Log('H %d %s fp=%s dmg=%s wtype=%s hull=%s wid=%s at=%s src=%s' % (t, sTarget, sFp, sDmg, sType, sHull, sWid, sAt, sSrc))
	pObject.CallNextHandler(pEvent)

def CollisionForce(pEvent):
	try:
		return '%.1f' % pEvent.GetCollisionForce()
	except:
		pass
	try:
		return '%.1f' % App.CollisionEventPtr(pEvent.this).GetCollisionForce()
	except:
		return '?'

def CollisionLine(sKind, pEvent):
	# a collision this instance detected (C) or a client's report the host applies (CH):
	# both objects, the force and the instance's own player ship
	global g_iRamHitAt
	t = Now()
	sA = Try(lambda e=pEvent: ObjName(e.GetSource()), '?')
	sB = Try(lambda e=pEvent: ObjName(e.GetDestination()), '?')
	sOwn = '-'
	pGame = App.Game_GetCurrentGame()
	if pGame and pGame.GetPlayer():
		sOwn = ShipName(pGame.GetPlayer())
		if sKind == 'C' and sOwn in (sA, sB):
			g_iRamHitAt = t
	Log('%s %d %s %s force=%s own=%s' % (sKind, t, sA, sB, CollisionForce(pEvent), sOwn))

def OnCollision(pObject, pEvent):
	try:
		CollisionLine('C', pEvent)
	except:
		pass
	pObject.CallNextHandler(pEvent)

def OnHostCollision(pObject, pEvent):
	try:
		CollisionLine('CH', pEvent)
	except:
		pass
	pObject.CallNextHandler(pEvent)

def OnExploding(pObject, pEvent):
	try:
		Log('D %d %s fp=%d' % (Now(), ObjName(pEvent.GetDestination()), pEvent.GetFiringPlayerID()))
	except:
		Log('D %d ?' % Now())
	pObject.CallNextHandler(pEvent)

g_fDamageAt = -2.0

def DamageEnginesOnce(pPlayer):
	# BCMP_DAMAGE_AT=<s>, BCMP_DAMAGE=<fraction>: the host sets its own ship's impulse
	# engines to that fraction of their maximum once, <s> seconds after the start
	# (tests remote views of a ship with damaged engines; the host owns all damage)
	global g_fDamageAt
	if g_fDamageAt == -2.0:
		g_fDamageAt = string.atof(Env('BCMP_DAMAGE_AT', '-1'))
	if g_fDamageAt < 0 or g_sRole != 'host':
		return
	if (Now() - g_fStart) / 1000.0 < g_fDamageAt:
		return
	# BCMP_DAMAGE_EVERY=<s>: repeat it every <s> seconds (many deaths and respawns)
	fEvery = string.atof(Env('BCMP_DAMAGE_EVERY', '0'))
	if fEvery > 0:
		g_fDamageAt = g_fDamageAt + fEvery
	else:
		g_fDamageAt = -1.0
	fFrac = string.atof(Env('BCMP_DAMAGE', '0.2'))
	sKind = Env('BCMP_DAMAGE_KIND', 'impulse')
	if sKind == 'hull':
		# BCMP_DAMAGE_KIND=hull: set the hull of every other player's ship to that fraction
		for (iID, pShip) in GetShips():
			if (pPlayer is None or pShip.GetObjID() != pPlayer.GetObjID()) and pShip.GetHull():
				pHull = pShip.GetHull()
				pHull.SetCondition(pHull.GetMaxCondition() * fFrac)
				Log('X DAMAGE %d hull %s %.2f' % (Now(), ShipName(pShip), fFrac))
		return
	if pPlayer is None:
		return
	pImp = pPlayer.GetImpulseEngineSubsystem()
	if pImp:
		pImp.SetCondition(pImp.GetMaxCondition() * fFrac)
		Log('X DAMAGE %d impulse %.2f' % (Now(), fFrac))

g_fKickNext = -2.0

g_fRangeNext = 0.0
g_iRangeStep = -1

def RangeTest(pPlayer):
	# BCMP_RANGE (see InitEnv); returns 1 when it drives the ship
	global g_fRangeNext, g_iRangeStep, g_iNextVolley
	if not g_lRange:
		return 0
	pPlayer.SetImpulse(0.0, App.TGPoint3_GetModelForward(), App.PhysicsObjectClass.DIRECTION_MODEL_SPACE)
	if g_sRole != 'host' or InState() < 20.0:
		return 1
	import Multiplayer.MissionShared
	import loadspacehelper
	pSet = Multiplayer.MissionShared.g_pStartingSet
	if App.IsNull(pSet):
		return 1
	pTarget = App.ShipClass_GetObject(pSet, 'RT')
	if not pTarget:
		pTarget = loadspacehelper.CreateShip('Galor', pSet, 'RT', '')
		if not pTarget:
			return 1
		Log('X RTSHIP %d' % Now())
	t = Now()
	kP = pPlayer.GetWorldLocation()
	kF = pPlayer.GetWorldForwardTG()
	if t >= g_fRangeNext:
		g_iRangeStep = g_iRangeStep + 1
		g_fRangeNext = t + 15000
		if g_iRangeStep >= len(g_lRange):
			g_iRangeStep = 0
		d = g_lRange[g_iRangeStep]
		kT = MakePoint(kP.GetX() + kF.GetX() * d, kP.GetY() + kF.GetY() * d, kP.GetZ() + kF.GetZ() * d)
		pTarget.SetTranslate(kT)
		pTarget.SetVelocity(MakePoint(0.0, 0.0, 0.0))
		pTarget.UpdateNodeOnly()
		pSet.GetProximityManager().UpdateObject(pTarget)
		Log('X RANGE %d %.1f' % (t, d))
	pHull = pTarget.GetHull()
	if pHull:
		pHull.SetCondition(pHull.GetMaxCondition())
	pTarget.SetImpulse(0.0, App.TGPoint3_GetModelForward(), App.PhysicsObjectClass.DIRECTION_MODEL_SPACE)
	pPlayer.TurnTowardLocation(pTarget.GetWorldLocation())
	if pPlayer.GetTarget() is None or pPlayer.GetTarget().GetObjID() != pTarget.GetObjID():
		pPlayer.SetAlertLevel(App.ShipClass.RED_ALERT)
		pPlayer.SetTarget('RT')
	pSystem = pPlayer.GetWeaponSystemGroup(App.ShipClass.WG_PRIMARY)
	if pSystem and t >= g_iNextVolley:
		g_iNextVolley = t + 3000
		pSystem.StartFiring(pPlayer.GetTarget(), pPlayer.GetTargetOffsetTG())
		pSystem.SetForceUpdate(1)
	return 1

g_fSwitchNext = 0.0
g_iSwitchStep = -1

def SwitchTest(pPlayer):
	# BCMP_SWITCH (see InitEnv); returns 1 when it drives the ship
	global g_fSwitchNext, g_iSwitchStep
	if not g_bSwitch:
		return 0
	pPlayer.SetImpulse(0.0, App.TGPoint3_GetModelForward(), App.PhysicsObjectClass.DIRECTION_MODEL_SPACE)
	if g_sRole != 'host' or InState() < 20.0:
		return 1
	import Multiplayer.MissionShared
	import loadspacehelper
	pSet = Multiplayer.MissionShared.g_pStartingSet
	if App.IsNull(pSet):
		return 1
	lTargets = []
	for sName in ('RT1', 'RT2'):
		pT = App.ShipClass_GetObject(pSet, sName)
		if not pT:
			pT = loadspacehelper.CreateShip('Galor', pSet, sName, '')
			if not pT:
				return 1
			Log('X RTSHIP %d %s' % (Now(), sName))
		lTargets.append(pT)
	t = Now()
	kP = pPlayer.GetWorldLocation()
	kF = pPlayer.GetWorldForwardTG()
	kR = pPlayer.GetWorldRightTG()
	if t >= g_fSwitchNext:
		g_iSwitchStep = g_iSwitchStep + 1
		g_fSwitchNext = t + 12000
		# the active target 90 units ahead (a little to the side), the other 90 units behind
		iActive = g_iSwitchStep % 2
		for i in (0, 1):
			pT = lTargets[i]
			if i == iActive:
				f, s = 90.0, 15.0 * (1 - 2 * i)
			else:
				f, s = -90.0, 15.0 * (1 - 2 * i)
			kT = MakePoint(kP.GetX() + kF.GetX() * f + kR.GetX() * s, kP.GetY() + kF.GetY() * f + kR.GetY() * s,
				kP.GetZ() + kF.GetZ() * f + kR.GetZ() * s)
			pT.SetTranslate(kT)
			pT.SetVelocity(MakePoint(0.0, 0.0, 0.0))
			pT.UpdateNodeOnly()
			pSet.GetProximityManager().UpdateObject(pT)
		Log('X SWITCH %d %s' % (t, lTargets[iActive].GetName()))
		pSystem = pPlayer.GetWeaponSystemGroup(App.ShipClass.WG_PRIMARY)
		if pSystem:
			# both ships in the weapon system's target list; the banks keep firing
			pPlayer.SetAlertLevel(App.ShipClass.RED_ALERT)
			pPlayer.SetTarget(lTargets[1 - iActive].GetName())
			pSystem.StartFiring(pPlayer.GetTarget(), pPlayer.GetTargetOffsetTG())
			pPlayer.SetTarget(lTargets[iActive].GetName())
			pSystem.StartFiring(pPlayer.GetTarget(), pPlayer.GetTargetOffsetTG())
			pSystem.SetForceUpdate(1)
	for pT in lTargets:
		pHull = pT.GetHull()
		if pHull:
			pHull.SetCondition(pHull.GetMaxCondition())
		pT.SetImpulse(0.0, App.TGPoint3_GetModelForward(), App.PhysicsObjectClass.DIRECTION_MODEL_SPACE)
	return 1

def KickShip(pPlayer):
	# BCMP_KICK_AT=<s>, BCMP_KICK=<speed>, BCMP_KICK_EVERY=<s>: every player adds a
	# velocity of <speed> (half sideways, half backwards) to its own ship, first at
	# <s> + 4 * player index seconds, then every BCMP_KICK_EVERY seconds (simulates the
	# knockback of a big hit: the velocity is no longer along the nose)
	global g_fKickNext
	if g_fKickNext == -2.0:
		g_fKickNext = string.atof(Env('BCMP_KICK_AT', '-1'))
		if g_fKickNext >= 0 and g_sRole != 'host':
			g_fKickNext = g_fKickNext + 4.0 * string.atoi(('0' + g_sRole[6:]) or '0') + 4.0 * (g_sRole == 'client')
	if g_fKickNext < 0 or (Now() - g_fStart) / 1000.0 < g_fKickNext:
		return
	fEvery = string.atof(Env('BCMP_KICK_EVERY', '0'))
	if fEvery > 0:
		g_fKickNext = g_fKickNext + fEvery
	else:
		g_fKickNext = -1.0
	fKick = string.atof(Env('BCMP_KICK', '20'))
	kFwd = pPlayer.GetWorldForwardTG()
	kRight = pPlayer.GetWorldRightTG()
	kVel = pPlayer.GetVelocityTG()
	f = fKick * 0.7071
	kNew = App.TGPoint3()
	kNew.SetXYZ(kVel.GetX() + f * (kRight.GetX() - kFwd.GetX()), kVel.GetY() + f * (kRight.GetY() - kFwd.GetY()), kVel.GetZ() + f * (kRight.GetZ() - kFwd.GetZ()))
	pPlayer.SetVelocity(kNew)
	Log('X KICK %d %s %.1f' % (Now(), pPlayer.GetName(), fKick))

def HexLE16(iValue):
	return '%02x%02x' % (iValue & 255, (iValue >> 8) & 255)

def HexLE32(iValue):
	return '%02x%02x%02x%02x' % (iValue & 255, (iValue >> 8) & 255,
		(iValue >> 16) & 255, (iValue >> 24) & 255)

def AttackPacket(iSender, sBody):
	iLength = 3 + len(sBody) / 2
	return '%02x0132%s%s' % (iSender & 255, HexLE16(iLength), sBody)

def AttackFragment(iSender, iSequence, iIndex, iTotal, sBody):
	sFragment = '%02x' % (iIndex & 255)
	if iIndex == 0:
		sFragment = sFragment + ('%02x' % (iTotal & 255))
	iLength = 5 + len(sFragment) / 2 + len(sBody) / 2
	return '%02x0132%s%s%s%s' % (iSender & 255,
		HexLE16(0xa000 | iLength), HexLE16(iSequence), sFragment, sBody)

def AttackOnce():
	# BCMP_ATTACK=1: one client injects malformed transport frames plus validly
	# framed forged GAME messages after it has joined. The host must remain in
	# game and log every rejection; rawsend is available only in DLL test mode.
	global g_iAttack
	if g_iAttack < 0:
		g_iAttack = string.atoi(Env('BCMP_ATTACK', '0'))
	if g_iAttack != 1 or g_sRole != 'client' or InState() < 20.0:
		return
	g_iAttack = 2
	import bcmpdll
	pNet = App.g_kUtopiaModule.GetNetwork()
	pGame = App.Game_GetCurrentGame()
	pPlayer = pGame and pGame.GetPlayer()
	if not pNet or not pPlayer:
		Log('X ATTACK %d skipped no network/player' % Now())
		return
	iMe = pPlayer.GetNetPlayerID()
	iHost = pNet.GetHostID()
	# Shape/framing failures from an unbound endpoint.
	bcmpdll.rawsend('', g_sJoin, 22101, 1)
	bcmpdll.rawsend('ff', g_sJoin, 22101, 1)
	bcmpdll.rawsend('ffff02', g_sJoin, 22101, 1)
	bcmpdll.rawsend('ff01320000', g_sJoin, 22101, 1)
	bcmpdll.rawsend('ff0140', g_sJoin, 22101, 1)
	# A syntactically valid Connect with a name longer than PlayerNameMaxLength.
	sName = ''
	i = 0
	while i < 40:
		sName = sName + '4100'
		i = i + 1
	sConnect = '0000000078563412' + sName + '0000'
	bcmpdll.rawsend('ff0103' + HexLE16(3 + len(sConnect) / 2) + sConnect,
		g_sJoin, 22101, 1)
	# Exercise the GameSpy n+1 allocation at the exact 512-byte receive limit.
	bcmpdll.rawsend('5c' + ('41' * 511), g_sJoin, 22101, 1)
	# Correct endpoint, forged transport sender and forbidden GAME/script messages.
	bcmpdll.rawsend(AttackPacket(iHost, '0e00000000'), g_sJoin, 22101, 0)
	bcmpdll.rawsend(AttackPacket(iMe, '38'), g_sJoin, 22101, 0)
	bcmpdll.rawsend(AttackPacket(iMe, '2c' + HexLE32(iHost) + '0000'), g_sJoin, 22101, 0)
	bcmpdll.rawsend(AttackPacket(iMe, '2c' + HexLE32(iMe) + 'ff7f'), g_sJoin, 22101, 0)
	bcmpdll.rawsend(AttackPacket(iMe, '41' + HexLE32(iHost) + '07'), g_sJoin, 22101, 0)
	bcmpdll.rawsend(AttackPacket(iMe, '07' + ('00' * 24)), g_sJoin, 22101, 0)
	# Generic Event/Create are not safely attributable to a client.
	bcmpdll.rawsend(AttackPacket(iMe, '0c'), g_sJoin, 22101, 0)
	bcmpdll.rawsend(AttackPacket(iMe, '02'), g_sJoin, 22101, 0)
	# STATE Power tails are pairs of subsystem index and power byte. 255 cannot
	# name a stock ship subsystem and must be rejected before the native reader.
	sState = '07' + HexLE32(pPlayer.GetObjID()) + '00000000' + '80ff00'
	bcmpdll.rawsend(AttackPacket(iMe, sState), g_sJoin, 22101, 0)
	# A syntactically valid self-collision with FLT_MAX force must be rejected by
	# the mass-normalized collision-force cap before the native damage handler.
	iShip = pPlayer.GetObjID()
	sCollision = ('15' + HexLE32(0x8124) + HexLE32(0x800050) +
		HexLE32(iShip) + HexLE32(iShip) + '01' + '00000000' + 'ffff7f7f')
	bcmpdll.rawsend(AttackPacket(iMe, sCollision), g_sJoin, 22101, 0)
	# The first fragment is accepted into the reassembly window; replaying the
	# same sequence/index must be rejected as a duplicate.
	sFragment = AttackFragment(iMe, 0, 0, 2, '00')
	bcmpdll.rawsend(sFragment, g_sJoin, 22101, 0)
	bcmpdll.rawsend(sFragment, g_sJoin, 22101, 0)
	Log('X ATTACK %d sent sender=%d host=%d' % (Now(), iMe, iHost))

g_iInfoPaneTest = -1

def TestInfoPaneGuard():
	# BCMP_TEST_INFOPANE=1: call the mission's RebuildInfoPane once while the info pane
	# is None (the join race) and log whether it raised
	global g_iInfoPaneTest
	if g_iInfoPaneTest < 0:
		g_iInfoPaneTest = string.atoi(Env('BCMP_TEST_INFOPANE', '0'))
	if g_iInfoPaneTest != 1:
		return
	g_iInfoPaneTest = 2
	import Multiplayer.MissionMenusShared
	import Multiplayer.Episode.Mission1.Mission1Menus
	pShared = Multiplayer.MissionMenusShared
	pSaved = pShared.g_pInfoPane
	pShared.g_pInfoPane = None
	try:
		try:
			Multiplayer.Episode.Mission1.Mission1Menus.RebuildInfoPane()
			Log('X INFOPANE %d no exception' % Now())
		except:
			import sys
			Log('X INFOPANE %d exception %s %s' % (Now(), str(sys.exc_type), str(sys.exc_value)))
	finally:
		pShared.g_pInfoPane = pSaved

g_iRaise = -1

def OnTorpedoFired(pObject, pEvent):
	# BCMP_RAISE_ONCE=1: raise once from this event handler (tests the game's
	# exception dispatcher / debug console)
	global g_iRaise
	if g_iRaise < 0:
		g_iRaise = string.atoi(Env('BCMP_RAISE_ONCE', '0'))
	if g_iRaise == 1:
		g_iRaise = 2
		Log('X RAISE %d' % Now())
		raise 'bcmptest deliberate exception'
	try:
		Log('F %d %s' % (Now(), ObjName(pEvent.GetSource())))
	except:
		Log('F %d ?' % Now())
	pObject.CallNextHandler(pEvent)

def SkipIntro():
	import MainMenu.mainmenu
	pMM = MainMenu.mainmenu
	bAny = 0
	for sAttr in ('g_idOpeningSequence', 'g_idOpeningMovieSequence', 'g_idOpeningCreditsSequence'):
		if hasattr(pMM, sAttr) and getattr(pMM, sAttr) != App.NULL_ID:
			pSeq = App.TGSequence_Cast(App.TGObject_GetTGObjectPtr(getattr(pMM, sAttr)))
			if pSeq:
				pSeq.Skip()
			setattr(pMM, sAttr, App.NULL_ID)
			bAny = 1
	if bAny:
		pMM.FinishOpeningMovie(None)
		Log('X SKIPINTRO %d' % Now())
