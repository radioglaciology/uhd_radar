from ruamel.yaml import YAML
import sys
sys.path.append("preprocessing")
from generate_chirp import generate_chirp
from pathlib import Path

'''
All unit test functions must start with test_ and be in a file named test_*.py within the tests folder!
'''


def test_chirp_length():
    """ Tests the length of the chirp without zero-padding in default.yaml (20e-6 seconds) """
    # Load config file
    yaml_filename = Path(__file__).resolve().parent.parent.parent/"config"/"default.yaml"
    yaml = YAML(typ='safe')
    stream = open(yaml_filename)
    config = yaml.load(stream)

    # Create the chirp
    time_samples, _ = generate_chirp(config)

    # Test if the output time matches the config
    config_chirp_length = config['GENERATE']['chirp_length'] # [s]
    sample_rate = config['GENERATE']['sample_rate'] # [Hz]
    actual_chirp_length = len(time_samples)/sample_rate  # [s]

    assert config_chirp_length == actual_chirp_length

if __name__ == "__main__":
    # Manually test functions here
    test_chirp_length()
    print("All tests passed!")
