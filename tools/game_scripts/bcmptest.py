# bcmpfix test harness: automated multiplayer session + telemetry.
# Imported via Local.py (Autoexec does "import Local"); Tick() is called by bcmpfix (dinput.dll)
# when [Test] PythonTick is set. Inactive unless the BCMP_ROLE env var is set.
# Python 1.5 syntax (no +=, no list comprehensions, no string methods).
import App
import time
import string

# The environment is read through bcmpdll (registered by the DLL before the
# first Tick), because this module is imported earlier, at Autoexec time.
g_bInit = 0
g_sRole = ''
g_sName = 'Player'
g_sJoin = '127.0.0.1'
g_iSpecies = 1
g_iSystem = 1
g_sAI = 'attack'
g_fLogEvery = 0.1
g_fQuitAfter = 0.0

def Env(sKey, sDefault):
	import bcmpdll
	sVal = bcmpdll.getenv(sKey)
	if sVal is None:
		return sDefault
	return sVal

def InitEnv():
	global g_fStart, g_bInit, g_sRole, g_sName, g_sJoin, g_iSpecies, g_iSystem, g_sAI, g_fLogEvery, g_fQuitAfter
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

def Now():
	import bcmpdll
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
		App.g_kVarManager.SetStringVariable('Multiplayer', 'Mission', 'Multiplayer.Episode.Mission1.Mission1')
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
	if g_sRole == 'host':
		pMMS.SelectSystem(g_iSystem)
	else:
		if App.IsNull(Multiplayer.MissionShared.g_pStartingSet):
			return
	pMMS.SelectSpecies(g_iSpecies)
	SpawnShip()
	SetState('ingame')

def SpawnShip():
	import Multiplayer.MissionMenusShared
	import Multiplayer.Episode.Mission1.Mission1Menus
	pTop = App.TopWindow_GetTopWindow()
	pMultWindow = App.MultiplayerWindow_Cast(pTop.FindMainWindow(App.MWT_MULTIPLAYER))
	pMissionPane = pMultWindow.GetMissionPane()
	if pMissionPane:
		pMissionPane.SetNotVisible()
	pMMS = Multiplayer.MissionMenusShared
	Multiplayer.Episode.Mission1.Mission1Menus.StartMission(pMMS.g_iSpecies, pMMS.g_iSystem)
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
	return lShips

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
	global g_iManeuverEnd, g_sManeuver, g_iNextVolley, g_iVolleyEnd, g_iTargetID
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
	if t >= g_iManeuverEnd:
		r = Rand()
		if r < 0.5:
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
	if g_sManeuver == 'chase':
		kE = pEnemy.GetWorldLocation()
		kOff = MakePoint(kE.GetX() + 30.0 * (Rand() - 0.5), kE.GetY() + 30.0 * (Rand() - 0.5), kE.GetZ() + 30.0 * (Rand() - 0.5))
		pPlayer.TurnTowardLocation(kOff)
		pPlayer.SetImpulse(1.0, App.TGPoint3_GetModelForward(), App.PhysicsObjectClass.DIRECTION_MODEL_SPACE)
	elif g_sManeuver == 'jink':
		pPlayer.SetImpulse(0.8, App.TGPoint3_GetModelForward(), App.PhysicsObjectClass.DIRECTION_MODEL_SPACE)
	else:
		pPlayer.SetImpulse(0.0, App.TGPoint3_GetModelForward(), App.PhysicsObjectClass.DIRECTION_MODEL_SPACE)
	# weapons: torpedo + phaser volleys like a player holding the fire buttons
	if t >= g_iNextVolley:
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

def InGame():
	global g_fLastLog, g_iAIShipID
	pGame = App.Game_GetCurrentGame()
	if not pGame:
		return
	InstallHandlers()
	pPlayer = pGame.GetPlayer()
	if not pPlayer or pPlayer.IsDead():
		if InState() > 8.0:
			SpawnShip()
			SetState('ingame')
		return
	if g_sAI == 'attack':
		Pilot(pPlayer)
	if InState() > 5.0:
		TestInfoPaneGuard()
	DamageEnginesOnce(pPlayer)
	KickShip(pPlayer)
	t = Now()
	LogSubsystems(t)
	if t - g_fLastLog < g_fLogEvery * 1000.0:
		return
	g_fLastLog = t
	fGameTime = App.g_kUtopiaModule.GetGameTime()
	for (iID, pShip) in GetShips():
		kPos = pShip.GetWorldLocation()
		kVel = pShip.GetVelocityTG()
		fHull = -1.0
		pHull = pShip.GetHull()
		if pHull:
			fHull = pHull.GetCondition()
		iLocal = 0
		if pShip.GetObjID() == pPlayer.GetObjID():
			iLocal = 1
		kFwd = pShip.GetWorldForwardTG()
		Log('S %d %.3f %s %d %d %d %.2f %.2f %.2f %.2f %.2f %.2f %.1f %d %.3f %.3f %.3f' % (t, fGameTime, pShip.GetName(), iID, pShip.GetObjID(), iLocal, kPos.GetX(), kPos.GetY(), kPos.GetZ(), kVel.GetX(), kVel.GetY(), kVel.GetZ(), fHull, pShip.IsDying(), kFwd.GetX(), kFwd.GetY(), kFwd.GetZ()))

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
		Log('P %d %s %s' % (t, pShip.GetName(), string.join(lVals, ' ')))

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
	g_bHandlers = 1
	Log('X HANDLERS %d' % Now())

def ObjName(pObj):
	if not pObj:
		return '-'
	try:
		return App.ObjectClass_Cast(pObj).GetName()
	except:
		return '?'

def Try(pFunc, sDefault):
	try:
		return pFunc()
	except:
		return sDefault

def OnWeaponHit(pObject, pEvent):
	t = Now()
	sTarget = Try(lambda e=pEvent: ObjName(e.GetDestination()), '?')
	sFp = Try(lambda e=pEvent: '%d' % e.GetFiringPlayerID(), '?')
	sDmg = Try(lambda e=pEvent: '%.1f' % e.GetDamage(), '?')
	sType = Try(lambda e=pEvent: '%d' % e.GetWeaponType(), '?')
	sHull = Try(lambda e=pEvent: '%d' % e.IsHullHit(), '?')
	sWid = Try(lambda e=pEvent: '%d' % e.GetWeaponInstanceID(), '?')
	sAt = Try(lambda e=pEvent: '%.1f,%.1f,%.1f' % (e.GetWorldHitPoint().GetX(), e.GetWorldHitPoint().GetY(), e.GetWorldHitPoint().GetZ()), '?')
	Log('H %d %s fp=%s dmg=%s wtype=%s hull=%s wid=%s at=%s' % (t, sTarget, sFp, sDmg, sType, sHull, sWid, sAt))
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
	g_fDamageAt = -1.0
	fFrac = string.atof(Env('BCMP_DAMAGE', '0.2'))
	sKind = Env('BCMP_DAMAGE_KIND', 'impulse')
	if sKind == 'hull':
		# BCMP_DAMAGE_KIND=hull: set the hull of every other player's ship to that fraction
		for (iID, pShip) in GetShips():
			if pShip.GetObjID() != pPlayer.GetObjID() and pShip.GetHull():
				pHull = pShip.GetHull()
				pHull.SetCondition(pHull.GetMaxCondition() * fFrac)
				Log('X DAMAGE %d hull %s %.2f' % (Now(), pShip.GetName(), fFrac))
		return
	pImp = pPlayer.GetImpulseEngineSubsystem()
	if pImp:
		pImp.SetCondition(pImp.GetMaxCondition() * fFrac)
		Log('X DAMAGE %d impulse %.2f' % (Now(), fFrac))

g_fKickNext = -2.0

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
