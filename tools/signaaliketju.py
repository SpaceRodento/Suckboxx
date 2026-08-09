import math
# MPX4115 datasheet (Motorola/NXP Rev 3): Vout = Vs*(P*0.009 - 0.095)
# Vs 4.85..5.10..5.35 V | herkkyys 46 mV/kPa | vaste 1.0 ms | Io+ tyyp 0.1 mA
# datasheetin Fig.3 suosittaa n. 51 k kuormaa + 50 pF

def vout(P, Vs=5.1): return Vs*(P*0.009-0.095)

print("=== 1. ANTURIN ULOSTULO KAYTTOALUEELLA ===")
print(f"{'P abs kPa':>10} {'tila':<24} {'Vout@5.1V':>10} {'Vout@5.35V':>11}")
for P,n in [(15,"anturin alaraja"),(20,"suunnittelun alaraja"),(26,"joutok. syva pulssi"),
            (46,"joutok. keskiarvo"),(93,"taysi kaasu"),(101,"moottori seis"),
            (106,"paluuaallon huippu"),(115,"anturin ylaraja")]:
    print(f"{P:>10} {n:<24} {vout(P):>10.3f} {vout(P,5.35):>11.3f}")

print("\n=== 2. JANNITTEENJAKO 5V -> 3.3V ===")
V_abs_max = vout(115, 5.35)
print(f"Pahin mahdollinen Vout (P=115, Vs=5.35) = {V_abs_max:.3f} V")
print(f"{'R1(yla)':>8} {'R2(ala)':>8} {'k':>6} {'Vmax ADC':>9} {'kuorma uA':>10} {'Rth':>7}")
for R1,R2 in [(20e3,30e3),(12e3,20e3),(39e3,62e3),(10e3,15e3)]:
    k = R2/(R1+R2); I = V_abs_max/(R1+R2)*1e6; Rth = R1*R2/(R1+R2)
    # datasheetin Fig.3 oma kuormapiste on 51k eli ~98 uA -> se on sallittu toimintapiste,
    # ei ylaraja. Liialliseksi lasketaan selvasti sen yli menevat.
    flag = "  <-- valittu (= datasheetin 51k-piste)" if (R1,R2)==(20e3,30e3) else ("  YLIKUORMA" if I>120 else "  ok, mutta Rth korkea")
    print(f"{R1/1e3:>6.0f}k {R2/1e3:>6.0f}k {k:>6.3f} {V_abs_max*k:>9.3f} {I:>10.0f} {Rth/1e3:>5.1f}k{flag}")

R1,R2 = 20e3,30e3; k = R2/(R1+R2); Rth = R1*R2/(R1+R2)
print(f"\nValittu 20k/30k: k={k:.3f}, Rth={Rth/1e3:.0f}k, kokonaiskuorma {(R1+R2)/1e3:.0f}k")
print(f"  (datasheetin Fig.3 suosittaa ~51k -> jakaja TOIMII samalla kuormavastuksena)")
print(f"{'P abs kPa':>10} {'ADC-jannite':>12}")
for P in (20,26,46,93,101,115):
    print(f"{P:>10} {vout(P)*k:>12.3f}")

print("\n=== 3. ANTI-ALIAS RC ===")
print("Anturi on jo 1. asteen alipaasto: t_r=1.0ms (10-90%) -> f_c = 0.35/t_r")
print(f"  anturin oma f_c ~ {0.35/1e-3:.0f} Hz")
print("Naytteenotto fs=1000 Hz -> Nyquist 500 Hz. RC:n tehtava = estaa >500 Hz alias.")
print("HUOM: putkiresonanssia (90-215 Hz) EI poisteta analogisesti - se on alle Nyquistin")
print("      ja poistetaan digitaalisesti. Analoginen RC estaa VAIN aliasoitumisen.")
print(f"\n{'C':>8} {'f_c Hz':>8} {'vaim.@500Hz':>12} {'vaim.@50Hz(hyoty)':>18}")
for C in (33e-9,47e-9,68e-9,100e-9):
    fc = 1/(2*math.pi*Rth*C)
    a500 = -20*math.log10(math.sqrt(1+(500/fc)**2))
    a50  = -20*math.log10(math.sqrt(1+(50/fc)**2))
    m = "  <-- valittu" if C==68e-9 else ""
    print(f"{C*1e9:>6.0f}nF {fc:>8.0f} {a500:>11.1f}dB {a50:>17.2f}dB{m}")

print("\n=== 4. RESOLUUTIO ADC:N JALKEEN ===")
kpa_per_v = 1/(0.009*5.1)           # kaanteinen siirtofunktio
for bits,nimi in ((12,"ESP32 nimellinen 12-bit"),(9.5,"ESP32 realistinen ENOB")):
    lsb_kpa = (3.3/2**bits)/k*kpa_per_v
    print(f"  {nimi:<26} {lsb_kpa:>7.3f} kPa/LSB")
lsb = (3.3/2**9.5)/k*kpa_per_v
print(f"  D2-toleranssi 1.5 kPa vaatii ~10x paremman resoluution = 0.15 kPa")
for N in (1,16,64):
    print(f"    keskiarvo N={N:<3} -> {lsb/math.sqrt(N):.3f} kPa {'OK' if lsb/math.sqrt(N)<0.15 else 'ei riita'}")

print("\n=== 5. VIRTABUDJETTI ===")
loads = [("ESP32 WROOM-32, WiFi AP (piikki)",240),("3 x MPX4115AP @ 10 mA max",30),
         ("jakajat 3 x 100 uA",0.3),("LED + varaus",30)]
tot = sum(x[1] for x in loads)
for n,ma in loads: print(f"  {n:<36} {ma:>6.1f} mA @5V")
print(f"  {'YHTEENSA':<36} {tot:>6.1f} mA @5V = {tot*5/1000:.2f} W")
print(f"  12V-puolelta (buck 85% hyotysuhde): {tot*5/12/0.85:.0f} mA")
print(f"  Sulake: {tot*5/12/0.85*2/1000:.2f} A laskennallinen -> valitaan 1 A (hidas)")

print("\n=== 6. TASAJANNITEVIRHEEN VAIKUTUS (miksi kalibrointi on kanavien valinen) ===")
print(f"  Anturin oma tarkkuus +-1.5 kPa (max, 0-85C) = KANAVIEN VALINEN virhe jos ei kalibroida")
print(f"  D2-kynnys 1.5 kPa -> kalibroimaton laite voisi nayttaa 3.0 kPa eroa taysin synkatulla moottorilla")
print(f"  -> R4: 2-pisteen kanavakalibrointi PAKOLLINEN, ei valinnainen")
