"""PC-side small-signal display model inspired by ClusterFluxDSP.cpp.
Not an audio measurement or a nonlinear / sample-accurate DSP emulator.
"""
import math, cmath
FS=32000.0

def clip(x,a,b):return max(a,min(b,x))
def wave(t,kind):
 t%=1.0
 if kind==1:return 4*abs(t-.5)-1
 if kind==2:return 2*t-1
 if kind==3:return 1-2*t
 if kind==4:return 1.0 if t<.5 else -1.0
 return math.sin(2*math.pi*t)

def delays(v,phase,phase2):
 # Provisional control-to-delay mapping; verify against firmware mapping before calibration.
 base=.5+15*v[0]/127
 depth=clip(v[3]/127,0,1)
 stereo=clip((v[18]+v[19])/254,0,1)
 l=wave(phase,v[9]);r=wave(phase+stereo, v[9]);l2=wave(phase2,v[26])
 span=base*.8*depth
 dL=clip(base+span*l, .6,50)
 dR=clip(base+span*r, .6,50)
 if v[27]==1:
  dL+=l2*span*.20;dR+=l2*span*.20
 return dL,dR,l,r,l2

def response(v,delay_ms,channel,freqs,lfo2=0):
 """Linearized wet/dry comb + dual tap and approximate feedback damping.
 Matrix, saturation, feedback colour are nonlinear/coupled and NOT represented exactly.
 """
 mix=clip(v[4]/127,0,1)
 fb=clip((v[1]-64)/64*.9,-.92,.92)
 dual=clip(v[30]/127,0,1)
 damp=clip(v[32]/127,0,1)
 bbd=clip(v[15]/127,0,1)
 tz=clip(v[34]/127,0,1)
 delay_s=delay_ms/1000
 # Actual DSP uses fixed 1600 sample dry reference in through-zero mode.
 dry_s=tz*1600/FS
 wet_s=(1-tz)*delay_s+tz*(1600/FS+.7*(delay_s-.008))
 second_s=clip(delay_s*(1.21 if channel==0 else 1.17)+(13 if channel==0 else 19)/FS+(.28 if channel==0 else -.28)*lfo2*.008,.0006,.06)
 out=[]
 for f in freqs:
  z=cmath.exp(-2j*math.pi*f*wet_s)
  # Frequency-dependent feedback damping approximated with one-pole lowpass.
  pole=1/(1+1j*f/(200+8000*(1-damp)))
  g=fb*((1-damp)+damp*pole)
  wet=z/(1-g*z)
  if dual>0:
   z2=cmath.exp(-2j*math.pi*f*second_s)
   wet=(1-.48*dual)*wet+.48*dual*z2/(1-.65*g*z2)
  # Two BBD reconstruction poles, approximate cutoff tracking.
  cutoff=clip(.18*1024/delay_s,350,14500)
  lp=1/(1+1j*f/cutoff)
  wet*=((1-bbd)+bbd*lp*lp)
  dry=cmath.exp(-2j*math.pi*f*dry_s)
  h=(1-mix)*dry+mix*wet
  out.append(clip(20*math.log10(max(abs(h),1e-5)),-36,18))
 return out
