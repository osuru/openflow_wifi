/*
Автор: Ушакова М.В.
Дата создания программы: 	15.06.2013
Номер версии: 1.0
Дата последней модификации: */

/*
 * Copyright (C) 2013 Orenburg State University
 *https://github.com/osuru/odin-agent-osu
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


#include <cstring>
#include <netinet/in.h>
#include <stdexcept>
#include <stdint.h>
#include <string>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include <tbb/concurrent_hash_map.h>

#include "assert.hh"
#include "component.hh"
#include "vlog.hh"

#include "component.hh"
#include "config.h"

#include "netinet++/datapathid.hh"
#include "netinet++/ethernetaddr.hh"
#include "netinet++/ethernet.hh"

#include "openflow/openflow-event.hh"
#include "openflow/openflow-datapath-join-event.hh"
#include "openflow/openflow-datapath-leave-event.hh"

// для работы с JSON внешняя библиотека
#include "json_spirit.h"
// для работы с библиотеками java
#include <jni.h>  

using namespace vigil;
using namespace openflow;

namespace
{
static Vlog_module lg("virtap");

class VAPMaster:  public  Component  {
	public:
  void configure(const Configuration* c) 
  {  }
void install()
  {
  ifstream is( "virtap.conf" );
 Value value;
 read_stream( is, value );    

Object& rootObject = v.get_obj();

for( Object::size_type i=0;i<rootObject.size(); ++i )
{
    Pair& pair = rootObject[i];
    std::string& name = pair.name_; 
    Value& value = pair.value_;
     Std::string& func;

Array& SSID = v.get_array();
if( name == "SSID")
    {
    Value& val = rootArray[i];
     SSID = co[0].value_.get_str();
            }
if( name == "function")
{
  func= value.get_int();
   
}
  }
	
 IRestApiService restApi;

	 INoxProviderService noxProvider;
	 ScheduledExecutorService executor;
	
	  AgentManager agentManager;
	  ClientManager clientManager;	
	  VapManager VapManager;
	  PoolManager poolManager;
	
	 long subscriptionId = 0;
	 String subscriptionList = "";
	 int idleVapTimeout = 60; // Таймаут до точки
	
	  ConcurrentMap<Long, SubscriptionCallbackTuple> subscriptions = new ConcurrentHashMap<Long, SubscriptionCallbackTuple>();
    
	// Константы 
	static  String DEFAULT_POOL_FILE = "VAP.list"; 
	static  String DEFAULT_CLIENT_LIST_FILE = "VAP_client_list";  
	static  int DEFAULT_PORT = 2819; // Порт для общения с точкой
	
	  VAPMaster (const Context* c, const xercesc::DOMNode*) : Component(c){
		clientManager = new ClientManager();
		VapManager = new VapManager();
		poolManager = new PoolManager();
		agentManager = new AgentManager(clientManager, poolManager);
	}
	
	 VAPMaster(AgentManager agentManager, ClientManager clientManager, VapManager VapManager, PoolManager poolManager){
		this.agentManager = agentManager;
		this.clientManager = clientManager;
		this.VapManager = VapManager;
		this.poolManager = poolManager;
	}
	
	
	//********* Обработчики коммуникаций с точкой *********//
	
	/**
	 * Обработчик эхо-запроса от точки
	 * 
	 * InetAddress – ip клиента
	 */
	void receivePing ( InetAddress VAPAgentAddr) {
		if (agentManager.receivePing(VAPAgentAddr)) {
			// если это новая точка, добавляем в подписку
			IVAPAgent agent = agentManager.getAgent(VAPAgentAddr);
			pushSubscriptionListToAgent(agent);

			// Если точка незадействована то присоединяем к ней потоки
			for (VAPClient client: agent.getVapsLocal()) {
				executor.schedule(new IdleVapReclaimTask(client), idleVapTimeout, TimeUnit.SECONDS);
				
				// Сопоставляем таблицу потоков
				if (!client.getIpAddress().getHostAddress().equals("0.0.0.0")) {
					
					// получаем ссылку на конкретный объект точки
					VAPClient trackedClient = clientManager.getClients().get(client.getMacAddress());
					Vap Vap = trackedClient.getVap();
					assert (Vap != null);
					Vap.setOFMessageList(VapManager.getDefaultOFModList(client.getIpAddress()));
					
					// Добавляем потоки, ассоциированные с клиентом
        			try {
        				Vap.getAgent().getSwitch().write(Vap.getOFMessageList(), null);
        			} catch (IOException e) {
        				log.error("Failed to update switch's flow tables " + Vap.getAgent().getSwitch());
        			}
				}
			}
		}
		else {
			updateAgentLastHeard (VAPAgentAddr);
		}
	}
	
	/**
	 * Обработка Probe пакета от клиента
	 * 
	 *  VAPAgentAddr InetAddress – адрес точки
	 * clientHwAddress - MAC адрес клиента
	 */
	void receiveProbe ( InetAddress VAPAgentAddr,  MACAddress clientHwAddress, String ssid) {
		
		if (VAPAgentAddr == null
	    	|| clientHwAddress == null
	    	|| clientHwAddress.isBroadcast()
	    	|| clientHwAddress.isMulticast()
	    	|| agentManager.isTracked(VAPAgentAddr) == false
	    	|| poolManager.getNumNetworks() == 0) {
			return;
		}
		
		updateAgentLastHeard(VAPAgentAddr);
		
		/*
		 * Если это активный скан, генерируем ответ только на 1 точке
		 */
		if (ssid.equals("")) {
			// делаем ответ
			IVAPAgent agent = agentManager.getAgent(VAPAgentAddr);
			MACAddress bssid = poolManager.generateBssidForClient(clientHwAddress);
			
			Set<String> ssidSet = new TreeSet<String> ();
			for (String pool: poolManager.getPoolsForAgent(VAPAgentAddr)) {

				if (pool.equals(PoolManager.GLOBAL_POOL))
					continue;
				
				ssidSet.addAll(poolManager.getSsidListForPool(pool));
			}
			
			executor.execute(new VAPAgentSendProbeResponseRunnable(agent, clientHwAddress, bssid, ssidSet));
			
			return;
		}
				
		/*
		 * Клиент пытается подулючиться к конкретной SSID.
		 * Ищем пул и ассоциируем VAP с ним
		 */
		for (String pool: poolManager.getPoolsForAgent(VAPAgentAddr)) {
			if (poolManager.getSsidListForPool(pool).contains(ssid)) {
				VAPClient oc = clientManager.getClient(clientHwAddress);
		    	
		    	// Это первый раз?
		    	if (oc == null) {		    		
					List<String> ssidList = new ArrayList<String> ();
					ssidList.addAll(poolManager.getSsidListForPool(pool));
					
					Vap Vap = new Vap (poolManager.generateBssidForClient(clientHwAddress), ssidList);
					
					try {
						oc = new VAPClient(clientHwAddress, InetAddress.getByName("0.0.0.0"), Vap);
					} catch (UnknownHostException e) {
						e.printStackTrace();
					}
		    		clientManager.addClient(oc);
		    	}
		    	
		    	Vap Vap = oc.getVap();
		    	assert (Vap != null);
		    	
				if (Vap.getAgent() == null) {
					// Да, это первый раз,
					// Используем global pool для первого раза
					handoffClientToApInternal(PoolManager.GLOBAL_POOL, clientHwAddress, VAPAgentAddr);
				}
				
				poolManager.mapClientToPool(oc, pool);
				
				return;
			}
		}
	}
	

	/**
	 * Переключение клиента на новую точку при рооминге
	 * 
	 * newApIpAddr - IPv4 адрес новой точки
	 * hwAddrSta - Ethernet адрес VAP  клиента
	 */
	 void handoffClientToApInternal (String pool,  MACAddress clientHwAddr,  InetAddress newApIpAddr){
		
		if (pool == null || clientHwAddr == null || newApIpAddr == null) {
			log.error("null argument in handoffClientToAp(): pool:" + pool + "clientHwAddr: " + clientHwAddr + " newApIpAddr: " + newApIpAddr);
			return;
		}
		
		(this) {
		
			IVAPAgent newAgent = agentManager.getAgent(newApIpAddr);
			
			// Если на новой точке нет агента, игнорируем
			if (newAgent == null) {
				log.error("Handoff request ignored: VAPAgent " + newApIpAddr + " doesn't exist");
				return;
			}
			
			VAPClient client = clientManager.getClient(clientHwAddr);
			
			// Если клиент неизвестен, игнор
			if (client == null) {
				log.error("Handoff request ignored: VAPClient " + clientHwAddr + " doesn't exist");
				return;
			}
			
			Vap Vap = client.getVap();
			
			assert (Vap != null);
			
			/* Если это в первый раз, делаем VAP и ассоциируем с клиентом
			 */
			if (Vap.getAgent() == null) {
				log.info ("Client: " + clientHwAddr + " connecting for first time. Assigning to: " + newAgent.getIpAddress());
	
				// Добавляем базовые потоки OpenFLow для маршрутизации
				try {
					newAgent.getSwitch().write(Vap.getOFMessageList(), null);
				} catch (IOException e) {
					log.error("Failed to update switch's flow tables " + newAgent.getSwitch());
				}
	
				newAgent.addClientVap(client);
				Vap.setAgent(newAgent);
				executor.schedule(new IdleVapReclaimTask (client), idleVapTimeout, TimeUnit.SECONDS);
				return;
			}
			
			/* Если клиент уже переассоциирован, игнорируем
			 */
			InetAddress currentApIpAddress = Vap.getAgent().getIpAddress();
			if (currentApIpAddress.getHostAddress().equals(newApIpAddr.getHostAddress())) {
				log.info ("Client " + clientHwAddr + " is already associated with AP " + newApIpAddr);
				return;
			}
			
			/* Проверка ограничений
			 * 
			 * - newAP и oldAP могут одновременно сломаться
			 * - клиент может быть ассоциирован с 2мя пулами одновременно.
			 */
			
			String clientPool = poolManager.getPoolForClient(client);
			
			if (clientPool == null || !clientPool.equals(pool)) {
				log.error ("Cannot handoff client '" + client.getMacAddress() + "' from " + clientPool + " domain when in domain: '" + pool + "'");
			}
			
			if (! (poolManager.getPoolsForAgent(newApIpAddr).contains(pool)
					&& poolManager.getPoolsForAgent(currentApIpAddress).contains(pool)) ){
				log.info ("Agents " + newApIpAddr + " and " + currentApIpAddress + " are not in the same pool: " + pool);
				return;
			}
			
			// Отправляем потоки OpenFlow
			try {
				newAgent.getSwitch().write(Vap.getOFMessageList(), null);
			} catch (IOException e) {
				log.error("Failed to update switch's flow tables " + newAgent.getSwitch());
			}
			
			/* Удаляем VAP из старой точки, добавляем на новую
			 */
			Vap.setAgent(newAgent);
			executor.execute(new VAPAgentVapAddRunnable(newAgent, client));
			executor.execute(new VAPAgentVapRemoveRunnable(agentManager.getAgent(currentApIpAddress), client));
		}
	}
	
	//*Методы для внешних приложений (из IVAPApplicationInterface) *//
	
	/**
	 * Переключение на новую точку
	 * 
	 */
	
	 void handoffClientToAp (String pool,  MACAddress clientHwAddr,  InetAddress newApIpAddr){
		handoffClientToApInternal(pool, clientHwAddr, newApIpAddr);
	}
	
	
	/**
	 * Получить список клиентов для текущей VAP
	 * возвращаются MAC адреса 
	 *	 */
	
	 Set<VAPClient> getClients (String pool) {
		return poolManager.getClientsFromPool(pool);
	}
	
	
	/**
	 * Хеш VAPClient из MACAddress клиента
	 * 
	 */
	
	 VAPClient getClientFromHwAddress (String pool, MACAddress clientHwAddress) {
		VAPClient client = clientManager.getClient(clientHwAddress);
		return (client != null && poolManager.getPoolForClient(client).equals(pool)) ? client : null;
	}
	
	
	/**
	 * Получить статистику RX на прием
	 * 
	 */
	
	 Map<MACAddress, Map<String, String>> getRxStatsFromAgent (String pool, InetAddress agentAddr) {
		return agentManager.getAgent(agentAddr).getRxStats();		
	}
	
	
	/**
	 * Получить список VAP агентов на точках
	 */
	
	 Set<InetAddress> getAgentAddrs (String pool){
		return poolManager.getAgentAddrsForPool(pool);
	}
	
	
	/**
	 * Управление подписками
	 */
	
	 long registerSubscription (String pool,  VAPEventSubscription oes,  NotificationCallback cb) {
		assert (oes != null);
		assert (cb != null);
		SubscriptionCallbackTuple tup = new SubscriptionCallbackTuple();
		tup.oes = oes;
		tup.cb = cb;
		subscriptionId++;
		subscriptions.put(subscriptionId, tup);
		
		/**
		 * Обновляем список подписок
		 */
		subscriptionList = "";
		int count = 0;
		for (Entry<Long, SubscriptionCallbackTuple> entry: subscriptions.entrySet()) {
			count++;
			 String addr = entry.getValue().oes.getClient();
			subscriptionList = subscriptionList + 
								entry.getKey() + " " + 
								(addr.equals("*") ? MACAddress.valueOf("00:00:00:00:00:00") : addr)  + " " +
								entry.getValue().oes.getStatistic() + " " +
								entry.getValue().oes.getRelation().ordinal() + " " +
								entry.getValue().oes.getValue() + " ";
		}

		subscriptionList = String.valueOf(count) + " " + subscriptionList;

		
		for (InetAddress agentAddr : poolManager.getAgentAddrsForPool(pool)) {
			pushSubscriptionListToAgent(agentManager.getAgent(agentAddr));
		}
		
		return subscriptionId;
	}
	
	
	/**
	 * Удалить подписку
	 * 
	 */
	
	 void unregisterSubscription (String pool,  long id) {
		subscriptions.remove(id);
		
		subscriptionList = "";
		int count = 0;
		for (Entry<Long, SubscriptionCallbackTuple> entry: subscriptions.entrySet()) {
			count++;
			 String addr = entry.getValue().oes.getClient();
			subscriptionList = subscriptionList + 
								entry.getKey() + " " + 
								(addr.equals("*") ? MACAddress.valueOf("00:00:00:00:00:00") : addr)  + " " +
								entry.getValue().oes.getStatistic() + " " +
								entry.getValue().oes.getRelation().ordinal() + " " +
								entry.getValue().oes.getValue() + " ";
		}

		subscriptionList = String.valueOf(count) + " " + subscriptionList;

		for (InetAddress agentAddr : poolManager.getAgentAddrsForPool(pool)) {
			pushSubscriptionListToAgent(agentManager.getAgent(agentAddr));
		}
	}
	

	/**
	 * Добавить SSID к VAP
	 * 
	 */
	
	 boolean addNetwork (String pool, String ssid) {
		if (poolManager.addNetworkForPool(pool, ssid)) {
			
			for(VAPClient oc: poolManager.getClientsFromPool(pool)) {
				Vap Vap = oc.getVap();
				assert (Vap != null);
				Vap.getSsids().add(ssid);
				
				IVAPAgent agent = Vap.getAgent();
				
				if (agent != null) {
					agent.updateClientVap(oc);
				}
			}
			
			return true;
		}
		
		return false;
	}
	
	
	/**
	 * Удалить SSID из VAP
	 * 
	 */
	
	 boolean removeNetwork (String pool, String ssid) {
		if (poolManager.removeNetworkFromPool(pool, ssid)){
			
			for (VAPClient oc: poolManager.getClientsFromPool(pool)) {
				
				Vap Vap = oc.getVap();
				assert (Vap != null);
				Vap.getSsids().remove(ssid);
				
				IVAPAgent agent = Vap.getAgent();
				
				if (agent != null) {
					agent.updateClientVap(oc);
				}
			}
			
			return true;
		}
			
		return false;
	}
	

	
	 Collection<Class< extends INoxService>> getModuleDependencies() {
		Collection<Class< extends INoxService>> l =
	        new ArrayList<Class< extends INoxService>>();
	    l.add(INoxProviderService.class);
        l.add(IRestApiService.class);
		return l;
	}

	 Collection<Class< extends INoxService>> getModuleServices() {
		return null;
	}

	
	 Map<Class< extends INoxService>, INoxService> getServiceImpls() {
		Map<Class< extends INoxService>,
        INoxService> m =
        new HashMap<Class< extends INoxService>,
        INoxService>();
        m.put(VAPMaster.class, this);
        return m;
	}

	
	 void init(NoxModuleContext context)
			throws NoxModuleException {
		noxProvider = context.getServiceImpl(INoxProviderService.class);
		restApi = context.getServiceImpl(IRestApiService.class);
		IThreadPoolService tp = context.getServiceImpl(IThreadPoolService.class);
		executor = tp.getScheduledExecutor();
	}

	
	 void startUp(NoxModuleContext context) {		
		noxProvider.addOFSwitchListener(this);
		noxProvider.addOFMessageListener(OFType.PACKET_IN, this);
		restApi.addRestletRoutable(new VAPMasterWebRoutable());
		
		agentManager.setNoxProvider (noxProvider);
		
		// Парзим конфиг
        Map<String, String> configOptions = context.getConfigParams(this);
        
        
        // список доверенных точек
        String agentAuthListFile = DEFAULT_POOL_FILE;
        String agentAuthListFileConfig = configOptions.get("virtap.conf");
        
        if (agentAuthListFileConfig != null) {
        	agentAuthListFile = agentAuthListFileConfig; 
        }
        
        List<VAPApplication> applicationList = new ArrayList<VAPApplication>();
       	try {
			BufferedReader br = new BufferedReader (new FileReader(agentAuthListFile));
			
			String strLine;
			
			/* Формат такой
			 * 
			 * IP_точки  pool1 pool2 pool3 ...
			 */
			while ((strLine = br.readLine()) != null) {
				if (strLine.startsWith("#")) // Это закоментировано
					continue;
				
				if (strLine.length() == 0) // Пустое
					continue;
				
				String [] fields = strLine.split(" "); 
				if (!fields[0].equals("NAME")) {
					log.error("Missing NAME field " + fields[0]);
					log.error("Offending line: " + strLine);
					System.exit(1);
				}
				
				if (fields.length != 2) {
					log.error("A NAME field should specify a single string as a pool name");
					log.error("Offending line: " + strLine);
					System.exit(1);
				}

				String poolName = fields[1];
				
				strLine = br.readLine();
				
				if (strLine == null) {
					log.error("Unexpected EOF after NAME field for pool: " + poolName);
					System.exit(1);
				}
				
				fields = strLine.split(" ");
				
				if (!fields[0].equals("NODES")){
					log.error("A NAME field should be followed by a NODES field");
					log.error("Offending line: " + strLine);
					System.exit(1);
				}
				
				if(fields.length == 1) {				
					log.error("A pool must have at least one node defined for it");
					log.error("Offending line: " + strLine);
					System.exit(1);
				}
				
				for (int i = 1; i < fields.length; i++) {
					poolManager.addPoolForAgent(InetAddress.getByName(fields[i]), poolName);
				}
				
				// Сети
				strLine = br.readLine();
				
				if (strLine == null) {
					log.error("Unexpected EOF after NODES field for pool: " + poolName);
					System.exit(1);
				}

				fields = strLine.split(" ");
				
				if (!fields[0].equals("NETWORKS")) {
					log.error("A NODES field should be followed by a NETWORKS field");
					log.error("Offending line: " + strLine);
					System.exit(1);
				}
				
				for (int i = 1; i < fields.length; i++) {
					poolManager.addNetworkForPool(poolName, fields[i]);
				}
				
				// Приложения  
				strLine = br.readLine();
				
				if (strLine == null) {
					log.error("Unexpected EOF after NETWORKS field for pool: " + poolName);
					System.exit(1);
				}

				fields = strLine.split(" ");
				
				if (!fields[0].equals("APPLICATIONS")) {
					log.error("A NETWORKS field should be followed by an APPLICATIONS field");
					log.error("Offending line: " + strLine);
					System.exit(1);
				}
				
				for (int i = 1; i < fields.length; i++) {
					VAPApplication appInstance = (VAPApplication) Class.forName(fields[i]).newInstance();
					appInstance.setVAPInterface(this);
					appInstance.setPool(poolName);
					applicationList.add(appInstance);
				}
			}
			
      br.close();

		} catch (FileNotFoundException e1) {
			log.error("Agent authentication list (config option poolFile) not supplied. Terminating.");
			System.exit(1);
		} catch (IOException e) {
			e.printStackTrace();
			System.exit(1);
		} catch (InstantiationException e) {
			e.printStackTrace();
		} catch (IllegalAccessException e) {			
			e.printStackTrace();
		} catch (ClassNotFoundException e) {
			e.printStackTrace();
		}

        // Статические клиенты
        String clientListFile = DEFAULT_CLIENT_LIST_FILE;
        String clientListFileConfig = configOptions.get("clientList");
        
        if (clientListFileConfig != null) {
            clientListFile = clientListFileConfig;
        }
        
        try {
			BufferedReader br = new BufferedReader (new FileReader(clientListFile));
			
			String strLine;
			
			while ((strLine = br.readLine()) != null) {
				String [] fields = strLine.split(" ");
				
				MACAddress hwAddress = MACAddress.valueOf(fields[0]);
				InetAddress ipaddr = InetAddress.getByName(fields[1]);
				
				ArrayList<String> ssidList = new ArrayList<String> ();
				ssidList.add(fields[3]); // FIXME: assumes a single ssid
				Vap Vap = new Vap(MACAddress.valueOf(fields[2]), ssidList);

				log.info("Adding client: " + fields[0] + " " + fields[1] + " " +fields[2] + " " +fields[3]);
				clientManager.addClient(hwAddress, ipaddr, Vap);
				Vap.setOFMessageList(VapManager.getDefaultOFModList(ipaddr));
			}

      br.close();

		} catch (FileNotFoundException e) {
			// ничего не делать
		} catch (IOException e) {
			e.printStackTrace();
		}

        // Vap таймаут, порт, список ssid 
        String timeoutStr = configOptions.get("idleVapTimeout");
        if (timeoutStr != null) {
        	int timeout = Integer.parseInt(timeoutStr);
        	
        	if (timeout > 0) {
        		idleVapTimeout = timeout;
        	}
        }
        
        int port = DEFAULT_PORT;
        String portNum = configOptions.get("masterPort");
        if (portNum != null) {
            port = Integer.parseInt(portNum);
        }
        
        IThreadPoolService tp = context.getServiceImpl(IThreadPoolService.class);
        executor = tp.getScheduledExecutor();
        // Выносим в отдельный поток
        executor.execute(new VAPAgentProtocolServer(this, port, executor));
        
        // Запускаем
        for (VAPApplication app: applicationList) {
        	executor.execute(app);
        }
	}

	 void removedSwitch(IOFSwitch sw) {
		//если свич не точка доступа, то ничего не делаем, иначе удаляем VAP
		 InetAddress switchIpAddr = ((InetSocketAddress) sw.getChannel().getRemoteAddress()).getAddress();
		agentManager.removeAgent(switchIpAddr);		
	}

	
	 Command receive(IOFSwitch sw, OFMessage msg, NoxContext cntx) {
		
		// Ответ на DHCP фрейм
		
		Ethernet frame = INoxProviderService.bcStore.get(cntx, 
                INoxProviderService.CONTEXT_PI_PAYLOAD);

		IPacket payload = frame.getPayload(); // IP
        if (payload == null)
        	return Command.CONTINUE;
        
        IPacket p2 = payload.getPayload(); // TCP or UDP
        
        if (p2 == null) 
        	return Command.CONTINUE;
        
        IPacket p3 = p2.getPayload(); // Application
        if ((p3 != null) && (p3 instanceof DHCP)) {
        	DHCP packet = (DHCP) p3;
        	try {

        		 MACAddress clientHwAddr = MACAddress.valueOf(packet.getClientHardwareAddress());
        		 VAPClient oc = clientManager.getClients().get(clientHwAddr);
        		
        		if (oc == null || oc.getVap().getAgent() == null || oc.getVap().getAgent().getSwitch() == null) {
        			return Command.CONTINUE;
        		}
        		
        		// Ищем Your-IP поле
        		if (packet.getYourIPAddress() != 0) {
        			
        			        			 byte[] arr = ByteBuffer.allocate(4).putInt(packet.getYourIPAddress()).array();
        			 InetAddress yourIp = InetAddress.getByAddress(arr);
        			
        			//Если IP совпадают, ничего не делаем
        			if (yourIp.equals(oc.getIpAddress())) {
        				return Command.CONTINUE;
        			}
        			
        			log.info("Updating client: " + clientHwAddr + " with ipAddr: " + yourIp);
        			oc.setIpAddress(yourIp);
        			oc.getVap().setOFMessageList(VapManager.getDefaultOFModList(yourIp));
        			
        			// Устанавливаем новые потоки для этого клиента
        			try {
        				oc.getVap().getAgent().getSwitch().write(oc.getVap().getOFMessageList(), null);
        			} catch (IOException e) {
        				log.error("Failed to update switch's flow tables " + oc.getVap().getAgent().getSwitch());
        			}
        			oc.getVap().getAgent().updateClientVap(oc);
        		}
        		
			} catch (UnknownHostException e) {
				e.printStackTrace();
			}
        }
		return Command.CONTINUE;
	}

	 boolean isCallbackOrderingPostreq(OFType type, String name) {
		return false;
	}

	
	 boolean isCallbackOrderingPrereq(OFType type, String name) {
		return false;
	}
	
	/**
	 * Посылаем список подписки агенту
	 * 
	 */
	 void pushSubscriptionListToAgent ( IVAPAgent oa) {
		oa.setSubscriptions(subscriptionList);
	}

	 void updateAgentLastHeard (InetAddress VAPAgentAddr) {
		IVAPAgent agent = agentManager.getAgent(VAPAgentAddr);
		
		if (agent != null) {
			// Update last-heard for failure detection
			agent.setLastHeard(System.currentTimeMillis());
		}
	}
	
	 class VAPAgentVapAddRunnable implements Runnable {
		 IVAPAgent oa;
		 VAPClient oc;
		
		VAPAgentVapAddRunnable(IVAPAgent newAgent, VAPClient oc) {
			this.oa = newAgent;
			this.oc = oc;
		}
		
		 void run() {
			oa.addClientVap(oc);
		}
		
	}
	
	 class VAPAgentVapRemoveRunnable implements Runnable {
		 IVAPAgent oa;
		 VAPClient oc;
		
		VAPAgentVapRemoveRunnable(IVAPAgent oa, VAPClient oc) {
			this.oa = oa;
			this.oc = oc;
		}
		
		 void run() {
			oa.removeClientVap(oc);
		}
		
	}
	
	 class VAPAgentSendProbeResponseRunnable implements Runnable {
		 IVAPAgent oa;
		 MACAddress clientHwAddr;
		 MACAddress bssid;
		 Set<String> ssidList;
		
		VAPAgentSendProbeResponseRunnable(IVAPAgent oa, MACAddress clientHwAddr, MACAddress bssid, Set<String> ssidList) {
			this.oa = oa;
			this.clientHwAddr = clientHwAddr;
			this.bssid = bssid;
			this.ssidList = ssidList;
		}
		
		 void run() {
			oa.sendProbeResponse(clientHwAddr, bssid, ssidList);
		}
		
	}
	
	 class IdleVapReclaimTask implements Runnable {
		  VAPClient oc;
		
		IdleVapReclaimTask( VAPClient oc) {
			this.oc = oc;
		}
		
		
		 void run() {
			VAPClient client = clientManager.getClients().get(oc.getMacAddress());
			
			if (client == null) {
				return;
			}
			
			try {
				if (client.getIpAddress().equals(InetAddress.getByName("0.0.0.0"))) {
					IVAPAgent agent = client.getVap().getAgent();
					
					if (agent != null) {
						log.info("Clearing Vap " + client.getMacAddress() + 
								" from agent:" + agent.getIpAddress() + " due to inactivity");
						poolManager.removeClientPoolMapping(client);
						agent.removeClientVap(client);
						clientManager.removeClient(client.getMacAddress());
					}
				}
			} catch (UnknownHostException e) {
					}
		}
	}

	 class SubscriptionCallbackTuple {
		VAPEventSubscription oes;
		NotificationCallback cb;
	}
}

    REGISTER_COMPONENT(Simple_component_factory<virtap>,
		     virtap);
}
