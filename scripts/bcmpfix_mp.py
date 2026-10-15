# bcmpfix_mp - script layer of the bcmpfix multiplayer fixes (Python 1.5).
#
# Ticked by bcmpfix (dinput.dll) on the main thread; does nothing outside
# multiplayer games. Two jobs:
#
# 1. Host-authoritative torpedo hits: the host reports every torpedo hit on a
#    ship (target, shooter, world hit point). MP clients ignore their own
#    torpedo/ship collisions (DLL hook) and replay the host's hit with their
#    local copy of the torpedo at the reported point, so every player sees the
#    same hits and misses as the host, which decides the damage anyway.
#
# 2. Latency measurement for latency compensation: peers ping each other once a
#    second; the DLL advances remote ships by the one-way latency to the peer
#    their state updates come from (host: owning client, client: host).
#
# All players need this file and the DLL; the features follow bcmpfix.ini.
import App
import sys

MSG_BASE = App.MAX_MESSAGE_TYPES + 120
MSG_HIT = MSG_BASE
MSG_PING = MSG_BASE + 1
MSG_PONG = MSG_BASE + 2

PING_INTERVAL_MS = 1000
LEAD_INTERVAL_MS = 500
LEAD_EXTRA_MS = 30          # half a state update interval plus about one frame
HIT_SEARCH_RADIUS = 250.0   # max distance between host hit point and local torpedo copy
WAIT_MS = 400               # how long a hit report waits for our copy of the torpedo

g_idMission = None
g_bHostHasMod = 0
g_dRtt = {}                 # net id -> smoothed round trip time in ms
g_iNextPing = 0
g_iNextLead = 0
g_dHitSent = {}             # host: torpedo id -> ms of its hit report
g_iErrors = 0
g_dPending = {}             # client: torpedo id -> [deadline ms, attempt, target id, rel x, y, z, hull]
g_lWaiting = []             # client: hit reports without a local copy yet: [deadline ms, target, shooter, rel x, y, z, hull]
g_dStats = {'hits_sent': 0, 'hits_recv': 0, 'replayed': 0, 'no_torpedo': 0, 'no_target': 0, 'expired': 0, 'lost': 0, 'retries': 0, 'infopane_skipped': 0, 'waiting': 0, 'late_found': 0, 'moved_n': 0, 'moved_sum': 0, 'moved_max': 0}
g_iNextStats = 0


def Ms():
	import bcmpdll
	return bcmpdll.ms()

def Cfg(sKey):
	import bcmpdll
	return bcmpdll.config(sKey)

def DllLog(sText):
	import bcmpdll
	bcmpdll.dlllog(sText)

def Tick():
	global g_iErrors
	try:
		DoTick()
	except:
		import sys
		g_iErrors = g_iErrors + 1
		if g_iErrors <= 10:
			DllLog('bcmpfix_mp: %s %s' % (str(sys.exc_type), str(sys.exc_value)))

def DoTick():
	global g_iNextPing, g_iNextLead, g_iNextStats
	if not App.g_kUtopiaModule.IsMultiplayer():
		return
	GuardInfoPane()
	LogGuards()
	pNet = App.g_kUtopiaModule.GetNetwork()
	if not pNet:
		return
	import MissionLib
	pMission = MissionLib.GetMission()
	if not pMission:
		return
	if g_idMission != pMission.GetObjID():
		Register(pMission)
	RetryWaiting()
	ExpirePending()
	t = Ms()
	if t >= g_iNextPing:
		g_iNextPing = t + PING_INTERVAL_MS
		SendPings(pNet, t)
	if t >= g_iNextLead:
		g_iNextLead = t + LEAD_INTERVAL_MS
		if Cfg('LatencyCompensation') > 0:
			UpdateLeads(pNet)
	if Cfg('Verbose') > 0 and t >= g_iNextStats:
		g_iNextStats = t + 10000
		import bcmpdll
		DllLog('bcmpfix_mp stats %s rtt=%s torp(suppressed allowed)=%s' % (str(g_dStats), str(g_dRtt), bcmpdll.torpstats()))

# ---- join fix -------------------------------------------------------------
# The missions' RebuildInfoPane (Mission1/2/3/5Menus) runs when a player ship is
# created and uses MissionMenusShared.g_pInfoPane without checking it. A ship that
# arrives while a client joins, before the pane exists, raises AttributeError: in
# windowed mode the game then freezes in its Python debug console, in fullscreen
# ObjectCreatedHandler stops before ResetEnemyFriendlyGroups (and the small UI font
# set by RebuildInfoPane stays). Skip the rebuild while there is no pane; the game
# rebuilds the pane when it creates it.

GUARDED_MENUS = ('Multiplayer.Episode.Mission1.Mission1Menus', 'Multiplayer.Episode.Mission2.Mission2Menus',
		'Multiplayer.Episode.Mission3.Mission3Menus', 'Multiplayer.Episode.Mission5.Mission5Menus')
g_dGuarded = {}

def GuardInfoPane():
	# called by the DLL from inside the game's __import__: no import statements here
	for sName in GUARDED_MENUS:
		if g_dGuarded.has_key(sName) or not sys.modules.has_key(sName):
			continue
		pMod = sys.modules[sName]
		if pMod is None or not hasattr(pMod, 'RebuildInfoPane'):
			continue
		pMod.RebuildInfoPane = MakeInfoPaneGuard(pMod.RebuildInfoPane)
		g_dGuarded[sName] = 1
		g_lGuardLog.append(sName)

# The menus module is often imported by the failing handler itself, so the DLL calls
# GuardInfoPane right after every import of a "...Menus" module (hook in the game's
# __import__, src/scripterr.c); the tick is only a fallback. A Python-level
# __import__ wrapper does not work here: the game's __import__ takes the caller's
# frame globals and adds a dummy fromlist when they are missing.
g_lGuardLog = []            # names guarded, logged by the next tick

def LogGuards():
	while g_lGuardLog:
		DllLog('bcmpfix_mp: guarded %s.RebuildInfoPane' % g_lGuardLog[0])
		del g_lGuardLog[0]

def MakeInfoPaneGuard(pOrig):
	# Python 1.5 has no closures: pass the original through a default argument
	def RebuildInfoPane(pOrig=pOrig):
		import Multiplayer.MissionMenusShared
		if Multiplayer.MissionMenusShared.g_pInfoPane is None:
			g_dStats['infopane_skipped'] = g_dStats['infopane_skipped'] + 1
			return
		pOrig()
	return RebuildInfoPane

def Register(pMission):
	global g_idMission, g_bHostHasMod
	g_idMission = pMission.GetObjID()
	# client: suppress local torpedo hits only once the host proved (by answering
	# a ping) that it runs bcmpfix and will report its hits
	g_bHostHasMod = 0
	import bcmpdll
	bcmpdll.authority(0)
	App.g_kEventManager.AddBroadcastPythonFuncHandler(App.ET_NETWORK_MESSAGE_EVENT, pMission, __name__ + '.OnNetMessage')
	App.g_kEventManager.AddBroadcastPythonFuncHandler(App.ET_WEAPON_HIT, pMission, __name__ + '.OnWeaponHit')
	DllLog('bcmpfix_mp: handlers registered (host=%d)' % App.g_kUtopiaModule.IsHost())

# ---- messages -------------------------------------------------------------

def NewMessage(iType, bGuaranteed):
	pMessage = App.TGMessage_Create()
	pMessage.SetGuaranteed(bGuaranteed)
	kStream = App.TGBufferStream()
	kStream.OpenBuffer(64)
	kStream.WriteChar(chr(iType))
	return (pMessage, kStream)

def Finish(pMessage, kStream):
	pMessage.SetDataFromStream(kStream)
	kStream.CloseBuffer()

def OnNetMessage(pObject, pEvent):
	try:
		pMessage = pEvent.GetMessage()
		if App.IsNull(pMessage):
			return
		kStream = pMessage.GetBufferStream()
		iType = ord(kStream.ReadChar())
		if iType == MSG_HIT:
			ReceiveHit(kStream)
		elif iType == MSG_PING:
			ReceivePing(pMessage, kStream)
		elif iType == MSG_PONG:
			ReceivePong(pMessage, kStream)
		kStream.Close()
	except:
		import sys
		LogError('OnNetMessage', sys.exc_type, sys.exc_value)

# ---- latency ------------------------------------------------------------------

def SendPings(pNet, t):
	if App.g_kUtopiaModule.IsHost():
		iLocal = pNet.GetLocalID()
		pList = pNet.GetPlayerList()
		i = 0
		while i < pList.GetNumPlayers():
			iID = pList.GetPlayerAtIndex(i).GetNetID()
			if iID != iLocal:
				SendPing(pNet, iID, t)
			i = i + 1
	else:
		SendPing(pNet, pNet.GetHostID(), t)

def SendPing(pNet, iTo, t):
	(pMessage, kStream) = NewMessage(MSG_PING, 0)
	kStream.WriteInt(t)
	Finish(pMessage, kStream)
	pNet.SendTGMessage(iTo, pMessage)

def ReceivePing(pMessage, kStream):
	t = kStream.ReadInt()
	pNet = App.g_kUtopiaModule.GetNetwork()
	(pReply, kOut) = NewMessage(MSG_PONG, 0)
	kOut.WriteInt(t)
	Finish(pReply, kOut)
	pNet.SendTGMessage(pMessage.GetFromID(), pReply)

def ReceivePong(pMessage, kStream):
	global g_bHostHasMod
	iRtt = Ms() - kStream.ReadInt()
	if iRtt < 0 or iRtt > 5000:
		return
	iFrom = pMessage.GetFromID()
	if g_dRtt.has_key(iFrom):
		g_dRtt[iFrom] = (g_dRtt[iFrom] * 3 + iRtt) / 4
	else:
		g_dRtt[iFrom] = iRtt
	pNet = App.g_kUtopiaModule.GetNetwork()
	if not g_bHostHasMod and not App.g_kUtopiaModule.IsHost() and pNet and iFrom == pNet.GetHostID():
		g_bHostHasMod = 1
		import bcmpdll
		bcmpdll.authority(1)
		DllLog('bcmpfix_mp: host runs bcmpfix (rtt %d ms)' % iRtt)

def UpdateLeads(pNet):
	import bcmpdll
	pGame = App.MultiplayerGame_Cast(App.Game_GetCurrentGame())
	if not pGame:
		return
	pPlayer = pGame.GetPlayer()
	iOwn = App.NULL_ID
	if pPlayer:
		iOwn = pPlayer.GetObjID()
	if App.g_kUtopiaModule.IsHost():
		# updates about a client's ship come from that client
		for iID in g_dRtt.keys():
			pShip = pGame.GetShipFromPlayerID(iID)
			if pShip and pShip.GetObjID() != iOwn:
				bcmpdll.setlead(pShip.GetObjID(), g_dRtt[iID] / 2 + LEAD_EXTRA_MS)
	else:
		# all state updates come from the host
		iHost = pNet.GetHostID()
		if not g_dRtt.has_key(iHost) or not pPlayer:
			return
		iLead = g_dRtt[iHost] / 2 + LEAD_EXTRA_MS
		pSet = pPlayer.GetContainingSet()
		if not pSet:
			return
		pObj = pSet.GetFirstObject()
		if App.IsNull(pObj):
			return
		iFirst = pObj.GetObjID()
		iGuard = 0
		while iGuard < 500:
			pShip = App.ShipClass_Cast(pObj)
			if pShip and pShip.GetObjID() != iOwn:
				bcmpdll.setlead(pShip.GetObjID(), iLead)
			pObj = pSet.GetNextObject(pObj.GetObjID())
			if App.IsNull(pObj) or pObj.GetObjID() == iFirst:
				break
			iGuard = iGuard + 1

# ---- torpedo hits ---------------------------------------------------------------

# Event handlers must never raise: an uncaught exception in a game event handler
# opens the blocking "TG Python Exception Debug Console" and freezes the game.
def OnWeaponHit(pObject, pEvent):
	try:
		if App.g_kUtopiaModule.IsHost():
			SendHit(pEvent)
		elif g_dPending:
			# a replayed host hit just happened locally: retire that torpedo
			pTorp = App.Torpedo_Cast(pEvent.GetSource())
			if pTorp and g_dPending.has_key(pTorp.GetObjID()):
				del g_dPending[pTorp.GetObjID()]
				import bcmpdll
				bcmpdll.retire(pTorp.this)
				g_dStats['replayed'] = g_dStats['replayed'] + 1
	except:
		import sys
		LogError('OnWeaponHit', sys.exc_type, sys.exc_value)

def LogError(sWhere, eType, eValue):
	global g_iErrors
	g_iErrors = g_iErrors + 1
	if g_iErrors <= 20:
		DllLog('bcmpfix_mp %s: %s %s' % (sWhere, str(eType), str(eValue)))

def SendHit(pEvent):
	if not App.g_kUtopiaModule.IsHost() or Cfg('TorpedoHitAuthority') <= 0:
		return
	if pEvent.GetWeaponType() != 1:
		return
	pTarget = App.ShipClass_Cast(pEvent.GetDestination())
	pTorp = App.Torpedo_Cast(pEvent.GetSource())
	if not pTarget or not pTorp:
		return
	iTorp = pTorp.GetObjID()
	t = Ms()
	# the stock collision code may apply one torpedo twice (shield, then the part that
	# bleeds through to the hull). Disruptors fire in pairs, so the events of two
	# projectiles interleave: remember every reported torpedo, not only the last one
	if g_dHitSent.has_key(iTorp) and t - g_dHitSent[iTorp] < 1000:
		return
	if len(g_dHitSent) > 64:
		for iOld in g_dHitSent.keys():
			if t - g_dHitSent[iOld] > 2000:
				del g_dHitSent[iOld]
	g_dHitSent[iTorp] = t
	# GetWorldHitPoint() is not usable from script; at hit time the torpedo sits
	# on the impact point anyway
	kP = pTorp.GetWorldLocation()
	(pMessage, kStream) = NewMessage(MSG_HIT, 1)
	kStream.WriteInt(pTarget.GetObjID())
	kStream.WriteInt(pTorp.GetParentID())
	kStream.WriteFloat(kP.GetX())
	kStream.WriteFloat(kP.GetY())
	kStream.WriteFloat(kP.GetZ())
	kStream.WriteChar(chr(pEvent.IsHullHit()))
	Finish(pMessage, kStream)
	pNet = App.g_kUtopiaModule.GetNetwork()
	if pNet:
		pNet.SendTGMessageToGroup('NoMe', pMessage)
		g_dStats['hits_sent'] = g_dStats['hits_sent'] + 1

def ReceiveHit(kStream):
	iTarget = kStream.ReadInt()
	iShooter = kStream.ReadInt()
	fX = kStream.ReadFloat()
	fY = kStream.ReadFloat()
	fZ = kStream.ReadFloat()
	iHull = ord(kStream.ReadChar())
	if App.g_kUtopiaModule.IsHost() or Cfg('TorpedoHitAuthority') <= 0:
		return
	g_dStats['hits_recv'] = g_dStats['hits_recv'] + 1
	pTarget = App.ShipClass_GetObjectByID(None, iTarget)
	if not pTarget:
		g_dStats['no_target'] = g_dStats['no_target'] + 1
		return
	# remember the impact point relative to the target: it keeps moving
	kC = pTarget.GetWorldLocation()
	lHit = [iTarget, iShooter, fX - kC.GetX(), fY - kC.GetY(), fZ - kC.GetZ(), iHull]
	if not StartReplay(pTarget, lHit):
		# The fire message of a client's torpedo comes from that client, the hit report
		# from the host, so the report can arrive before our copy exists: wait for it.
		g_lWaiting.append([Ms() + WAIT_MS] + lHit)
		g_dStats['waiting'] = g_dStats['waiting'] + 1

def StartReplay(pTarget, lHit):
	# lHit: [target id, shooter id, rel x, rel y, rel z, hull]
	kC = pTarget.GetWorldLocation()
	pTorp = FindTorpedo(pTarget, lHit[1], kC.GetX() + lHit[2], kC.GetY() + lHit[3], kC.GetZ() + lHit[4])
	if pTorp is None:
		return 0
	g_dPending[pTorp.GetObjID()] = [Ms() + 250, 0, lHit[0], lHit[2], lHit[3], lHit[4], lHit[5]]
	ReplayApproach(pTorp, pTarget, g_dPending[pTorp.GetObjID()])
	return 1

def RetryWaiting():
	global g_lWaiting
	if not g_lWaiting:
		return
	t = Ms()
	lKeep = []
	for lEntry in g_lWaiting:
		pTarget = App.ShipClass_GetObjectByID(None, lEntry[1])
		if not pTarget:
			g_dStats['no_target'] = g_dStats['no_target'] + 1
		elif StartReplay(pTarget, lEntry[1:]):
			g_dStats['late_found'] = g_dStats['late_found'] + 1
		elif t < lEntry[0]:
			lKeep.append(lEntry)
		else:
			g_dStats['no_torpedo'] = g_dStats['no_torpedo'] + 1
	g_lWaiting = lKeep

def ReplayApproach(pTorp, pTarget, lEntry):
	# Re-fly our copy into the host's impact point: start just outside it on the
	# line from the ship centre and head straight in. The stock collision code
	# (released for this torpedo only) then produces the hit within a frame.
	import bcmpdll
	import math
	kC = pTarget.GetWorldLocation()
	fRX = lEntry[3]
	fRY = lEntry[4]
	fRZ = lEntry[5]
	fLen = math.sqrt(fRX * fRX + fRY * fRY + fRZ * fRZ)
	if fLen < 0.001:
		fRX = 0.0
		fRY = 0.0
		fRZ = 1.0
		fLen = 1.0
	fNX = fRX / fLen
	fNY = fRY / fLen
	fNZ = fRZ / fLen
	kV = pTorp.GetVelocityTG()
	fSpeed = math.sqrt(kV.GetX() * kV.GetX() + kV.GetY() * kV.GetY() + kV.GetZ() * kV.GetZ())
	if fSpeed < 10.0:
		fSpeed = 10.0
	# later attempts start further out and aim a bit deeper
	fBack = fSpeed * 0.06 + 1.0 + lEntry[1] * 2.0
	fDeep = 1.0 - lEntry[1] * 0.15
	fX = kC.GetX() + fRX * fDeep
	fY = kC.GetY() + fRY * fDeep
	fZ = kC.GetZ() + fRZ * fDeep
	fSX = fX + fNX * fBack
	fSY = fY + fNY * fBack
	fSZ = fZ + fNZ * fBack
	if lEntry[1] == 0:
		# how far our copy is moved: distance between its path and the shooter's projectile
		kT = pTorp.GetWorldLocation()
		fD = math.sqrt((kT.GetX() - fSX) * (kT.GetX() - fSX) + (kT.GetY() - fSY) * (kT.GetY() - fSY) + (kT.GetZ() - fSZ) * (kT.GetZ() - fSZ))
		g_dStats['moved_n'] = g_dStats['moved_n'] + 1
		g_dStats['moved_sum'] = g_dStats['moved_sum'] + int(fD * 100)
		if fD * 100 > g_dStats['moved_max']:
			g_dStats['moved_max'] = int(fD * 100)
	pTorp.SetTranslateXYZ(fSX, fSY, fSZ)
	kNewV = App.TGPoint3()
	kNewV.SetXYZ(-fNX * fSpeed, -fNY * fSpeed, -fNZ * fSpeed)
	pTorp.SetVelocity(kNewV)
	pTorp.UpdateNodeOnly()
	# mode 1: host saw a shield hit, mode 2: hull hit - the DLL makes ours match;
	# the start point becomes the start of the hit-test segment
	if lEntry[6]:
		bcmpdll.allowhit(pTorp.GetObjID(), 2, fSX, fSY, fSZ)
	else:
		bcmpdll.allowhit(pTorp.GetObjID(), 1, fSX, fSY, fSZ)

def ExpirePending():
	# replays without a local hit get two more approaches; after that the
	# torpedo is retired anyway (the host decided it hit)
	if not g_dPending:
		return
	import bcmpdll
	t = Ms()
	for iID in g_dPending.keys():
		lEntry = g_dPending[iID]
		if t < lEntry[0]:
			continue
		pTorp = App.Torpedo_GetObjectByID(None, iID)
		pTarget = App.ShipClass_GetObjectByID(None, lEntry[2])
		if pTorp and pTarget and lEntry[1] < 2:
			lEntry[1] = lEntry[1] + 1
			lEntry[0] = t + 250
			g_dStats['retries'] = g_dStats['retries'] + 1
			ReplayApproach(pTorp, pTarget, lEntry)
			continue
		del g_dPending[iID]
		if pTorp:
			bcmpdll.retire(pTorp.this)
			g_dStats['expired'] = g_dStats['expired'] + 1
		else:
			g_dStats['lost'] = g_dStats['lost'] + 1

def FindTorpedo(pTarget, iShooter, fX, fY, fZ):
	pSet = pTarget.GetContainingSet()
	if not pSet:
		return None
	pObj = pSet.GetFirstObject()
	if App.IsNull(pObj):
		return None
	iFirst = pObj.GetObjID()
	pBest = None
	fBest = HIT_SEARCH_RADIUS * HIT_SEARCH_RADIUS
	iGuard = 0
	while iGuard < 1000:
		pTorp = App.Torpedo_Cast(pObj)
		# a torpedo that already replays another hit is taken: pulse weapons fire in
		# pairs, and both reports would otherwise pick the same nearest copy
		if pTorp and pTorp.GetParentID() == iShooter and not g_dPending.has_key(pTorp.GetObjID()):
			kP = pTorp.GetWorldLocation()
			fDX = kP.GetX() - fX
			fDY = kP.GetY() - fY
			fDZ = kP.GetZ() - fZ
			fD = fDX * fDX + fDY * fDY + fDZ * fDZ
			if fD < fBest:
				fBest = fD
				pBest = pTorp
		pObj = pSet.GetNextObject(pObj.GetObjID())
		if App.IsNull(pObj) or pObj.GetObjID() == iFirst:
			break
		iGuard = iGuard + 1
	return pBest

