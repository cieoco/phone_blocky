"""Build referee web assets, including any operator-supplied local recordings."""
Import("env")
env.Replace(PROJECT_DATA_DIR=env.subst("$PROJECT_DIR/referee_data"))
