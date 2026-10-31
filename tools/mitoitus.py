import math

print("=== 1. SIGNAALIN PERUSTAAJUUDET (3-syl 4-tahti) ===")
print(f"{'rpm':>6} {'kanava Hz':>10} {'jakso ms':>9} {'imusarja Hz':>12}")
for rpm in (500, 800, 900, 1200, 3000, 6000):
    f_ch = rpm / 120.0          # 1 imutahti / 2 kierrosta / sylinteri
    print(f"{rpm:>6} {f_ch:>10.2f} {1000/f_ch:>9.1f} {f_ch*3:>12.2f}")

print("\n=== 2. NAYTTEENOTTOVAATIMUS ===")
f_idle, f_max = 800/120.0, 6000/120.0
print(f"Joutokaynti  {f_idle:.2f} Hz | Taysi kaasu {f_max:.1f} Hz")
for h in (1,2,3,5,10):
    print(f"  harmoninen #{h:<2} @6000rpm = {f_max*h:6.1f} Hz -> Nyquist vaatii >{2*f_max*h:.0f} Hz")

print("\n=== 3. HX710B RIITTAVYYS ===")
for sps in (10, 40):
    for rpm in (800, 6000):
        f = rpm/120.0
        n = sps/f
        alias = "ALIASOITUU (f > fs/2)" if f > sps/2 else f"{n:.1f} nayt./jakso"
        print(f"  {sps:>2} SPS @ {rpm:>4} rpm ({f:5.2f} Hz): {alias}")
print("  Nyrkkisaanto aaltomuodon rekonstruktioon: >=20 nayt./jakso")
print(f"  -> @6000rpm (50 Hz) tarvitaan {50*20:.0f} SPS/kanava")
print(f"  -> @800rpm  (6.7 Hz) tarvitaan {6.67*20:.0f} SPS/kanava")

print("\n=== 4. PUTKIRESONANSSI ('urkupilli') ===")
c = 360.0  # m/s, lammin ilma ~50C
print(f"{'letku m':>8} {'1/4-aalto Hz':>13} {'Helmholtz Hz':>13}")
V_dead = 1.0e-6  # anturin kammio ~1 cm3
for L in (0.3, 0.5, 1.0, 1.5, 2.0):
    for d_mm in (3.0,):
        A = math.pi*(d_mm/2000.0)**2
        f_quarter = c/(4*L)
        f_helm = (c/(2*math.pi))*math.sqrt(A/(V_dead*(L+0.6*d_mm/2000.0)))
        print(f"{L:>8.1f} {f_quarter:>13.0f} {f_helm:>13.0f}")
print(f"  Hyotysignaali 6.7-50 Hz -> resonanssit ovat 2-20x korkeammalla = EROTETTAVISSA")

print("\n=== 5. LETKUN PITUUSEROJEN VAIKUTUS ===")
for dL in (0.05, 0.10, 0.20):
    f1, f2 = c/(4*1.0), c/(4*(1.0+dL))
    print(f"  1.00 m vs {1+dL:.2f} m: resonanssi {f1:.0f} Hz vs {f2:.0f} Hz = {100*(f1-f2)/f1:.1f}% ero")

print("\n=== 6. ANTURIN MITTA-ALUE ===")
p_atm = 101.3
print(f"{'tila':<28} {'abs kPa':>9} {'gauge kPa':>10}")
for nimi, g in [("Moottori sammuksissa",0),("Joutokaynti (tyyp.)",-55),
                ("Joutokaynti (syva pulssi)",-75),("Taysi kaasu",-8),("Pulssin huippu",+5)]:
    print(f"{nimi:<28} {p_atm+g:>9.1f} {g:>10.1f}")
print(f"  HX710B 0-40 kPa: kattaa {0} .. {40} kPa -> EI kata mitaan yllaolevaa alipainetta")
print(f"  Tarve: n. 20-110 kPa abs (= -80..+9 kPa gauge)")

print("\n=== 7. ADC-RESOLUUTIO (MPX4115A + ESP32) ===")
span_kpa, span_v = 115-15, 4.8-0.2
for bits, nimi in ((12,"ESP32 nimellinen"),(9.5,"ESP32 todellinen ENOB"),(16,"ADS1115")):
    lsb_v = 3.3/(2**bits)
    lsb_kpa = lsb_v*(5.0/3.3)*(span_kpa/span_v)
    print(f"  {nimi:<24} {lsb_kpa:>7.3f} kPa/LSB")
lsb = (3.3/2**9.5)*(5/3.3)*(span_kpa/span_v)
for N in (1, 100, 1000):
    print(f"  keskiarvo N={N:<5} -> {lsb/math.sqrt(N):.4f} kPa (hajonta pienenee sqrt(N))")
print("  Synkronoinnin tarkkuustarve tyypillisesti 0.5-1 kPa -> reilusti riittava")

print("\n=== 8. RPM-TARKKUUS JAKSONAIKAMITTAUKSELLA ===")
for fs in (200, 500, 1000, 2000):
    for rpm in (800,):
        T = 1.0/(rpm/120.0)
        dT = 1.0/fs
        print(f"  fs={fs:>4} Hz: jakso {T*1000:.1f} ms, 1 nayte = {dT*1000:.2f} ms = +-{rpm*dT/T:.1f} rpm (raaka)")
print("  Interpolointi nollanylityksessa parantaa ~10x; keskiarvo yli N jakson /sqrt(N)")

print("\n=== 9. TIEDONSIIRTOBUDJETTI ===")
for fs in (200, 500, 1000):
    raw = fs*3*2  # 3 kanavaa, 2 tavua
    print(f"  fs={fs:>4} Hz raakadata: {raw/1024:6.1f} kB/s = {raw*8/1000:5.0f} kbit/s (WiFi ok, SD ok)")
print(f"  60 s nauhoitus @1000 Hz = {1000*60*3*2/1024/1024:.1f} MB")
print("  UI-paivitys 20 Hz riittaa silmalle -> ala striimaa raakaa, striimaa johdettua")
