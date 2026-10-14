import sys
import json
import re
import pandas as pd

def parse_line(line):
    '''
    From a string of gps information, this will parse out unix_time, lat, lon, alt, 
    altMSL, ecefx, ecefy, and ecefz information and return it.
    '''
    # Regex to extract UNIX time (e.g., 1764000892.487560)
    unix_time_match = re.search(r'(\d+\.\d+)', line)
    if not unix_time_match:
        return None
    unix_time = float(unix_time_match.group(1))

    # Regex to extract the entire JSON object starting with "class":"TPV"
    json_match = re.search(r'\{.*?"class":"TPV".*?\}', line, re.DOTALL)
    if not json_match:
        return None
    json_str = json_match.group(0)

    try:
        data = json.loads(json_str)
    except json.JSONDecodeError:
        return None

    # Extract the required fields
    lat = data.get('lat')
    lon = data.get('lon')
    alt = data.get('alt')
    altMSL = data.get('altMSL')
    ecefx = data.get('ecefx')
    ecefy = data.get('ecefy')
    ecefz = data.get('ecefz')

    return {
        'unix_time': unix_time,
        'lat': lat,
        'lon': lon,
        'alt': alt,
        'altMSL': altMSL,
        'ecefx': ecefx,
        'ecefy': ecefy,
        'ecefz': ecefz,
    }

def extract_locations_from_gpspipe(lines):
    '''
    From a series of lines seperated by a newline, if TPV is in a line, the parse_line function
    is run. If results are returned, it is added to a list of dictionaries. The list is then converted
    to DataFrame and returned. It can also be converted to a CSV if a line is uncommented. 
    '''
    results = []

    for line in lines.split("\n"):
        if 'TPV' in line:
            result = parse_line(line)
            if result:
                results.append(result)
    
    # Convert list of dicts to DataFrame
    df = pd.DataFrame(results)

    # Uncomment below to save as CSV
    # df.to_csv('output.csv', index=False)
    return df
    