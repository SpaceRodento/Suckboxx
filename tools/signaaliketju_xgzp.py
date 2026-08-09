import math
# XGZP6847A100KPGN33 (CFSensor, datasheet V2.7):
#   gauge/vacuum -100..0 kPa, analoginen, 3.3 Vdc
#   3.3V-syotto: Vout = 2.7 + K*P, K = 0.025 V/kPa  (datasheetin kaava P=(Vout-2.7)/K)
#   ulostuloalue 0.2..2.7 V | tarkkuus +-2 %FSS | T90 = 2.5 ms | Ivdd 2 mA
K, V0 = 0.025, 2.7
def vout(P): return V0 + K*P          # P = gauge kPa (negatiivinen = alipaine)

print("=== 1. ULOSTULO KAYTTOALUEELLA (3.3V-versio) ===")
print(f"{'P gauge':>9} {'P abs':>7} {'tila':<26} {'Vout':>7} {'huom':<22}")
for Pg,n in [(-100,"anturin alaraja"),(-75,"joutok. syva pulssi"),(-55,"joutok. keskiarvo"),
             (-8,"taysi kaasu"),(0,"moottori seis / letku irti"),(5,"paluuaallon huippu")]:
    v = vout(Pg); huom = "SATUROITUU 2.7 V" if Pg>0 else ""
    print(f"{Pg:>9} {101.3+Pg:>7.1f} {n:<26} {min(v,2.7):>7.3f} {huom:<22}")
print(f"\nHerkkyys {K*1000:.0f} mV/kPa. Kayttoalue -75..0 kPa = {vout(-75):.3f}..{vout(0):.3f} V")
print("ESP32 ADC1 (11 dB vaim.) lineaarinen alue n. 0.15..3.10 V -> osuu keskelle, EI jakajaa")

print("\n=== 2. ANTI-ALIAS RC (lahde = anturin DAC, matala impedanssi) ===")
print(f"Anturin oma vaste T90 = 2.5 ms -> tau = 2.5/2.303 = {2.5/2.303:.2f} ms")
print(f"  -> anturin oma f_c = {1/(2*math.pi*2.5e-3/2.303):.0f} Hz (1. asteen napa, jo mukana)")
fs = 1000
print(f"Naytteenotto fs = {fs} Hz -> Nyquist {fs/2:.0f} Hz")
print(f"\n{'R':>7} {'C':>7} {'f_c':>7} {'@50Hz':>8} {'@500Hz RC':>10} {'@500Hz RC+anturi':>18}")
f_sens = 1/(2*math.pi*2.5e-3/2.303)
for R,C in [(4.7e3,100e-9),(10e3,100e-9),(10e3,47e-9),(22e3,100e-9)]:
    fc = 1/(2*math.pi*R*C)
    a  = lambda f,c: -20*math.log10(math.sqrt(1+(f/c)**2))
    m = "  <-- valittu" if (R,C)==(10e3,100e-9) else ""
    print(f"{R/1e3:>6.1f}k {C*1e9:>5.0f}nF {fc:>6.0f}Hz {a(50,fc):>7.2f}dB {a(500,fc):>9.1f}dB {a(500,fc)+a(500,f_sens):>17.1f}dB{m}")
print("HUOM: @50 Hz vaimennus on SAMA kaikissa kanavissa -> kumoutuu erotuksessa (D3)")

print("\n=== 3. RESOLUUTIO ===")
for bits,nimi in ((12,"ESP32 nimellinen"),(9.5,"ESP32 realistinen ENOB")):
    print(f"  {nimi:<24} {(3.3/2**bits)/K:>6.3f} kPa/LSB")
lsb = (3.3/2**9.5)/K
print(f"  D2-kynnys 1.5 kPa -> tavoite <=0.15 kPa (1/10 kynnyksesta)")
for N in (1,4,16,64):
    print(f"    keskiarvo N={N:<3} -> {lsb/math.sqrt(N):>5.3f} kPa  {'OK' if lsb/math.sqrt(N)<=0.15 else 'ei riita'}")
print(f"  fs=1000 Hz, N=16 -> paivitysnopeus {1000/16:.0f} Hz (UI tarvitsee ~20 Hz)")

print("\n=== 4. VIRTABUDJETTI ===")
tot5 = 240 + 30
print(f"  ESP32 WROOM-32 WiFi AP piikki      240 mA @5V")
print(f"  LED + varaus                        30 mA @5V")
print(f"  3 x XGZP6847A @ 2 mA (3.3V-kiskolta) 6 mA @3.3V = {6*3.3/5:.1f} mA @5V ekviv.")
print(f"  YHTEENSA n. {tot5+4:.0f} mA @5V = {(tot5+4)*5/1000:.2f} W")
print(f"  12V-puolelta buck 85%: {(tot5+4)*5/12/0.85:.0f} mA -> sulake 1 A hidas")

print("\n=== 5. RATIOMETRISYYS: MIKSI D3 SUOJAA MEITA ===")
print("  Anturi on ratiometrinen: Vout skaalautuu 3.3V-kiskon mukana.")
print("  WiFi-lahetys kuormittaa kiskoa -> kaikki KOLME kanavaa heiluvat SAMAAN suuntaan.")
print("  D3 (poikkeama masterista) vahentaa kanavat toisistaan -> yhteismuotoinen heilunta kumoutuu.")
dv = 0.05  # 50 mV dippi 3.3V-kiskossa
print(f"  Esim. {dv*1000:.0f} mV dippi kiskossa = {dv/K:.1f} kPa virhe absoluuttiseen lukemaan,")
print(f"                                       mutta ~0 kPa virhe kanavien EROON.")

print("\n=== 6. LETKU (datasheet: suositeltu sisahalkaisija 2.5 mm) ===")
c = 360.0
A = math.pi*(2.5/2000.0)**2
print(f"{'pituus':>8} {'1/4-aalto':>11} {'Helmholtz':>11}")
for L in (0.5,0.8,1.0,1.5):
    fh = (c/(2*math.pi))*math.sqrt(A/(1.0e-6*(L+0.6*2.5/2000.0)))
    print(f"{L:>7.1f}m {c/(4*L):>10.0f}Hz {fh:>10.0f}Hz")
print(f"  Kaikki >> hyotysignaali (6.7-50 Hz) ja < Nyquist(500 Hz) -> poistetaan DIGITAALISESTI")
