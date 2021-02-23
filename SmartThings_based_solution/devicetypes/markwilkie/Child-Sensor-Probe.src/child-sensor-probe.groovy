/**
 *  Copyright 2015 SmartThings
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 *  in compliance with the License. You may obtain a copy of the License at:
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software distributed under the License is distributed
 *  on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License
 *  for the specific language governing permissions and limitations under the License.
 *
 */
metadata {
	definition (name: "Child Sensor Probe", namespace: "ogiewon", mnmn: "SmartThingsCommunity", vid: "02b84e8a-83ff-30e9-b20c-1450e6301518") {
		capability "radioamber53161.probemeasurement"
		capability "Sensor"
	}

	// UI tile definitions
	tiles(scale: 2) {
		multiAttributeTile(name: "probe", type: "generic", width: 6, height: 4, canChangeIcon: true) {
			tileAttribute("device.probe", key: "PRIMARY_CONTROL") {
				attributeState("probe", label: '${currentValue}', unit: "", defaultState: true)
			}
		}
	}
}

def parse(String description) {
	log.debug "parse(${description}) called"
	def parts = description.split(" ")
	def name  = parts.length>0?parts[0].trim():null
	//def value = parts.length>1?parts[1].trim():null
    def value = new BigDecimal(((parts.length>1?parts[1].trim():null) as float)).setScale(1, BigDecimal.ROUND_HALF_UP)
    if (name && value) {
		sendEvent(name: name, value: value, unit: units)
	}
}

def installed() {
}