import sys

sensorName = sys.argv[1]
sensorName = sensorName.upper()
print (sensorName)

friendlyName = sys.argv[2]
print (friendlyName)

print ("")
print ("")

formatedString = f'light\n\
  - platform: mqtt\n\
    schema: json\n\
    name: "{friendlyName} LED"\n\
    state_topic: "multi/{sensorName}/state"\n\
    command_topic: "multi/{sensorName}/set"\n\
    brightness: true\n\
    flash: true\n\
    rgb: true\n\
    optimistic: false\n\
    \n\
sensor:\n\
  - platform: mqtt\n\
    state_topic: "multi/{sensorName}/state"\n\
    name: "{friendlyName} PIR"\n\
    value_template: '
    
formatedString = formatedString + "'{{ value_json.motion }}'\n"

formatedString = formatedString + f'\n\
  - platform: mqtt\n\
    state_topic: "multi/{sensorName}/state"\n\
    name: "{friendlyName} Temperature"\n\
    unit_of_measurement: "°F"\n\
    value_template: '
formatedString = formatedString + "'{{ value_json.temperature | round(1) }}'\n"

formatedString = formatedString + f'\n\
  - platform: mqtt\n\
    state_topic: "multi/{sensorName}/state"\n\
    name: "{friendlyName} Feels Like"\n\
    unit_of_measurement: "°F"\n\
    value_template: '
formatedString = formatedString + "'{{ value_json.heatIndex | round(0) }}'\n"

formatedString = formatedString + f'\n\
  - platform: mqtt\n\
    state_topic: "multi/{sensorName}/state"\n\
    name: "{friendlyName} Humidity"\n\
    unit_of_measurement: "%"\n\
    value_template: '
formatedString = formatedString + "'{{ value_json.humidity | round(0) }}'\n"


formatedString = formatedString + "\n\n"


friendlyNameNoSpace = friendlyName.replace(" ", "_").lower()


formatedString = formatedString + f'{friendlyNameNoSpace}_multisensor:\n\
  name: {friendlyName} Multisensor\n\
  entities:\n\
    - sensor.{friendlyNameNoSpace}_temperature\n\
    - sensor.{friendlyNameNoSpace}_feels_like\n\
    - sensor.{friendlyNameNoSpace}_humidity\n\
    - sensor.{friendlyNameNoSpace}_pir  \n\
    - light.{friendlyNameNoSpace}_led\n'



print (formatedString)

