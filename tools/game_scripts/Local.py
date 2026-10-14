# bcmpfix test harness bootstrap (Python 1.5). Real code lives in bcmptest.py.
# Only does something when bcmpfix runs in test mode (env BCMP_ROLE set).
g_pMod = None
g_sErr = ''
try:
	import bcmptest
	g_pMod = bcmptest
except:
	import sys
	g_sErr = str(sys.exc_type) + ': ' + str(sys.exc_value)

def Tick():
	global g_sErr
	if g_sErr:
		import bcmpdll
		bcmpdll.dlllog('Local: bcmptest import failed: ' + g_sErr)
		g_sErr = ''
	if g_pMod is not None:
		g_pMod.Tick()
