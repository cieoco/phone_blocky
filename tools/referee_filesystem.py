"""Only the referee filesystem includes the original match recordings."""
Import("env")
env.Replace(PROJECT_DATA_DIR=env.subst("$PROJECT_DIR/referee_data"))
