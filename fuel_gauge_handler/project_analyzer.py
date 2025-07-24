import footprint_analyzer as fp
import symbol_analyzer as sym
import ThreeDsym_analyzer as threeD


# Run
fp.analyze("./PWM_Converter")
sym.analyze_kicad_symbols("./PWM_Converter")
threeD.analyze_3d_models("./PWM_Converter")