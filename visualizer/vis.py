/* Название: МОДУЛЬ ВИЗУАЛИЗАЦИИ РАБОТЫ КОРПОРАТИВНОЙ БЕСПРОВОДНОЙ ИНФРАСТРУКТУРЫ
Автор: Полежаев П.Н.
Дата создания программы: 25.06.2013
Номер версии: 1.0
Дата последней модификации:  06.07.2013*/
/*
 * Copyright (C) 2013 Orenburg State University
 *https://github.com/osuru/openflow_wifi/visualiser
 * Licensed under the Apache License, Version 2.0 (the "License");

 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0

 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,

 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.

 */

#for web
import web
#for json
import json
#for system utils
import sys
#Python Image Library
import PIL
#String stream
import StringIO
#Image - class for image loading/saving
#ImageDraw - class for image modification
from PIL import Image,ImageDraw

#urls mapping for web.py
urls = (
    '/', 'visualiser'
)	

#access points information file
access_points_file_name = "aps.json"
#maps information file
maps_file_name = "maps.json"
#access point radius on a map
ap_circle_radius = 50
#access point color on a map
ap_circle_color = (255, 0, 0)

#starting web.py web application
app = web.application(urls, globals())

#mobile client class
class client:
    #constructor
    def __init__(self, mac, lwap_mac, bssid):
        #mac-addres of mobile client
        self.mac = mac
        #lwap_mac of mobile client
        self.lwap_mac = lwap_mac
        #bssid of mobile client
        self.bssid = bssid

    #str saver
    def __str__(self):
        #string for identation of client data
        ident = "&nbsp;&nbsp;&nbsp;&nbsp;"
        #preparing string
        return "%smac : %s<br>%slwap_mac : %s<br>%sbssid : %s<br>" % (ident, self.mac, ident, self.lwap_mac, ident, self.bssid)

#client handling function 
def as_client(c):
    #checking existence of necessary parameters
    if 'mac' in c and 'lwap_mac' in c and 'bssid' in c:
        return client(c['mac'], c['lwap_mac'], c['bssid'])
    #rasing exception if failed
    raise KeyError('"mac", "lwap_mac" and "bssid" shoud present as client options in json!')

#access point (AP) class
class access_point:
    #constructor
    def __init__(self, mac, name, x, y, map_id, clients):
        #MAC of AP        
	self.mac = mac
        #name of AP
	self.name = name
        #map-image coordinates of AP
	self.x = x
	self.y = y
        #map identifier of AP
	self.map_id = map_id
        #list of connected clients
        self.clients = clients
    #str saver
    def __str__(self):
        #preparing main info about AP
        result = "mac : %s<br>name : %s<br>x : %d<br>y : %d<br>map_id : %s<br>clients : <br>" % (self.mac, self.name, self.x, self.y, self.map_id)
        #creation of client list
        for c in self.clients:
            result += "%s<br>" % str(c)       
	return result

#AP handling function
def as_access_point(ap):
    #checking correctness
    if 'mac' in ap and 'name' in ap and 'x' in ap and 'y' in ap and 'map_id' in ap:
        #cretion of clients list
        #set empty list
        clients = []
        #print str(ap)
        #check existence of clients parameter
        if 'clients' in ap:
            #for each client
            for c in ap['clients']:
                #add it to list
                clients.append(c)
        #call constructor of AP
        return access_point(ap['mac'], ap['name'], ap['x'], ap['y'], ap['map_id'], clients)
    #raising exception
    raise KeyError('"mac", "name", "x", "y" and "map_id" shoud present as access point options in json!')

#object handling function
def object_hook(obj):
    #if __type__ is key of obj
    if '__type__' in obj:
        #for AP
        if obj['__type__'] == 'access_point':
            #call AP handling function
            return as_access_point(obj)
        #for client
        elif obj['__type__'] == 'client':
            #call client handling function
            return as_client(obj)
        #for other objects
        else:
            #just return them
            return obj;
    #raising exception
    raise KeyError('"__type__" shoud present as option of any object in json!')
    
#request processing class for web.py
class visualiser:
    #method for loading APs or maps config
    # file_name - name of the file
    # type - type of data ("ap" for APs, "map" for maps)    
    def load_json(self, file_name, type = "ap"):
        try:
            #creation of file reader
            json_data = open(file_name, 'r')
            #print json_data
            if type == 'ap':
                #load AP data with hook function
             	res = json.load(json_data, object_hook=object_hook)
            elif type == 'map':
                #load map data
	        res = json.load(json_data)
            else:
	        print 'Invalid type for json!!!';
            #close file reader
            json_data.close()
            return res
        #on file error
        except IOError:
            print 'file "%s" does not exist!' % file_name
            #sys.exit()
            #except TypeError:
                #    print 'file "%s" has invalid json format!' % file_name
            #    #sys.exit()

    #function for checking dublicates in ap_list and map_list
    def check_dublicates(self):
        #make empty dictionary
        aps = {}
        #for each AP
        for ap in ap_list:
            #if it not in aps
            if not ap.mac in aps:
                aps[ap.mac] = 1 
            else:
                #there is a dublicate
                return false
        #make empty dictionary
        maps = {}
        #for each map
        for m in map_list.keys():
            #if it not in maps
            if not m in maps:
                maps[m] = 1
            else:
                #there is a dublicate
                return false
        ##make empty clients
        #clients = {}
        ##for each AP
        #for ap in ap_list:
        ##    for each its client
        #    for c in ap.clients:
        ##        for first time
        #        if not c.mac in clients:
        #            clients[c.mac] = 1
        #        else:
        ##           there is a dublicate          
        #            return false
        #there is no dublicate
        return true

    #function for searching AP
    # mac - AP MAC
    def find_ap_by_mac(self, mac):
        #intialize search result
	ap = None
        #for each AP
	for a in self.ap_list:
            #if its MAC equls mac
	    if a.mac == mac:
               #save AP
	       ap = a
               break
        #return found AP or None
        return ap

    #constructor
    def __init__(self):
        #loading APs
	self.ap_list = self.load_json(access_points_file_name, 'ap')  
        #loading maps 
	self.map_list = self.load_json(maps_file_name, 'map')
        #debug pringting
	print self.ap_list
	print self.map_list
           
    #GET-reguest processor 
    def GET(self):
        #Get parameters accessor
	i = web.input(action=None, id=None, mac=None)
        #if no action parameter specified
        if not i.action:
            #return help information
            return self.get_help()
        #information about AP with given MAC
	elif i.action=='info':
	    return self.get_ap_info(i.mac)
        #map with APs
	elif i.action=='map':
     	    return self.get_map(i.id)
        #maps list
        elif i.action=='list':
            return self.get_map_list()
        #APs list
	elif i.action=='aplist':
	    return self.get_ap_list()
      
    #get help information
    def get_help(self):
        #prepare content-type for HTML
        web.header('Content-type','text/html')        

        #prepare help text
        result =  'This is visualization module for conrol system of wireless infrastructure.<br><br>'
        result += 'Available parameters : <br>'
        result += '?action=info&mac=... - Get information about access point with given MAC<br>'
        result += '?action=map&id=...   - Get map with drawn accesss points by id<br>'
        result += '?action=list         - Get maps list<br>'
        result += '?action=aplist       - Get access points list<br><br>'
        
        #return help text
        return result

    #get information about AP
    # mac - AP MAC
    def get_ap_info(self, mac):
        #check mac
	if not mac:
            #return error string
	    return 'Error! Please specify "info" parameter!!!'
	else:
            #find AP by its MAC
	    ap = self.find_ap_by_mac(mac)
            #if there is no such AP
	    if not ap:
                #return error string
	        return 'Error! Please specify correct value of parameter "mac"!!!'
            #prepare content-type for HTML
	    web.header('Content-type','text/html')
            #return string with AP information        
	    return str(ap)

    #get image stream for map
    # id - map id
    def get_map(self, id):
        #check id
	if not id:
           #return error string
	   return 'Error! Please specify "id" parameter!!!'
        else:          
           #check map with neccessary id
	   if id in self.map_list:
                #get path to map
	        path = self.map_list[id];
	   else:
                #return error string
	        return 'Error! Please specify correct "id" parameter!!!'
	   
	   try:
                #load map image
                image = Image.open(path) 
                #create draw instance
                draw = ImageDraw.Draw(image)
                #for each AP in list
	        for ap in self.ap_list:
                    #if its map_id equls id
		    if ap.map_id==id:
                        #draw_circle                        
		        draw.ellipse((ap.x-ap_circle_radius, ap.y-ap_circle_radius, ap.x+ap_circle_radius, ap.y+ap_circle_radius), fill=ap_circle_color)
                #create string stream for saving image
	        png_stream = StringIO.StringIO()
                #save image
	        image.save(png_stream, format="PNG")
                #get image string
	        contents = png_stream.getvalue()

                #create HTTP headers
                #image type
                web.header('Content-type','image/png')
                #encoding
                web.header('Content-transfer-encoding','binary')
                #file name
	        web.header('Content-Disposition', 'attachment; filename="%s"' % path) 
                #return open(path, 'rb').read()
                #return image as stream
	        return contents
           #on file error
	   except IOError:
                #return error message
	        return 'Error! Cannot load image from file "%s"!!!' % path
        
    #get list of all maps
    def get_map_list(self):
        #make result string empty
	result = ''
        #for each id of map
        for id in self.map_list.keys():
            #add map info to result
  	    result += "%s,%s\n" % (id,self.map_list[id])
        #return HTML text
	return result

    #get list of all aps
    def get_ap_list(self):
        #make result string empty
	result = ''
        #for each AP
	for ap in self.ap_list:
            #add its info to result
	    result += str(ap) + "<br>"
        #set HTML type
        web.header('Content-type','text/html')
        #return HTML text
	return result

if __name__ == "__main__":
    #run web.py application
    app.run()
