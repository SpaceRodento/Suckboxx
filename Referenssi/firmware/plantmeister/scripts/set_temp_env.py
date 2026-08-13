Import("env")

import os
import platform

# Some Windows setups fail XIAO link phase when TEMP/TMP points to C:\\WINDOWS\\TEMP.
# Force user temp for build tools to keep linker response files stable.
if platform.system() == "Windows":
    user_temp = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Temp")
    if user_temp:
        os.environ["TEMP"] = user_temp
        os.environ["TMP"] = user_temp
        print("[set_temp_env] TEMP/TMP set to:", user_temp)
