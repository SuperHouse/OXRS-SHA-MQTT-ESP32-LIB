/*
 * OXRS_MQTT.cpp
 * 
 */

#include "Arduino.h"
#include "OXRS_MQTT.h"

OXRS_MQTT::OXRS_MQTT(PubSubClient& client) 
{
  this->_client = &client;
}

void OXRS_MQTT::getJson(JsonObject * json)
{
  json->getOrAddMember("connected").set(_client->connected());

  json->getOrAddMember("broker").set(_broker);
  json->getOrAddMember("port").set(_port);
  json->getOrAddMember("clientId").set(_clientId);
  
  // NOTE: we don't expose the password
  json->getOrAddMember("username").set(_username);
  
  json->getOrAddMember("topicPrefix").set(_topicPrefix);
  json->getOrAddMember("topicSuffix").set(_topicSuffix);
  
  // if we have a client id then add the various topics
  if (strlen(_clientId) > 0)
  {
    char topic[64];

    json->getOrAddMember("lwtTopic").set(getLwtTopic(topic));
    json->getOrAddMember("adoptTopic").set(getAdoptTopic(topic));

    json->getOrAddMember("configTopic").set(getConfigTopic(topic));
    json->getOrAddMember("commandTopic").set(getCommandTopic(topic));

    json->getOrAddMember("statusTopic").set(getStatusTopic(topic));
    json->getOrAddMember("telemetryTopic").set(getTelemetryTopic(topic));
  }  
}

void OXRS_MQTT::setJson(JsonObject * json)
{
  // broker is mandatory so don't clear if not explicitly specified
  if (json->containsKey("broker"))
  { 
    if (json->containsKey("port"))
    { 
      setBroker(json->getMember("broker"), json->getMember("port").as<uint16_t>());
    }
    else
    {
      setBroker(json->getMember("broker"), MQTT_DEFAULT_PORT);      
    }
  }
  
  // client id is mandatory so don't clear if not explicitly specified
  if (json->containsKey("clientId"))
  { 
    setClientId(json->getMember("clientId"));
  }
  
  if (json->containsKey("username") && json->containsKey("password"))
  { 
    setAuth(json->getMember("username"), json->getMember("password"));
  }
  else
  {
    setAuth(NULL, NULL);
  }
  
  if (json->containsKey("topicPrefix"))
  { 
    setTopicPrefix(json->getMember("topicPrefix"));
  }
  else
  {
    setTopicPrefix(NULL);
  }

  if (json->containsKey("topicSuffix"))
  { 
    setTopicSuffix(json->getMember("topicSuffix"));
  }
  else
  {
    setTopicSuffix(NULL);
  }
}

void OXRS_MQTT::setBroker(const char * broker, uint16_t port)
{
    strcpy(_broker, broker);
    _port = port;
}

void OXRS_MQTT::setClientId(const char * clientId)
{ 
  strcpy(_clientId, clientId);
}

void OXRS_MQTT::setAuth(const char * username, const char * password)
{
  if (username == NULL)
  {
    _username[0] = '\0';
    _password[0] = '\0';
  }
  else
  {
    strcpy(_username, username);
    strcpy(_password, password);
  }
}

void OXRS_MQTT::setTopicPrefix(const char * prefix)
{ 
  if (prefix == NULL)
  {
    _topicPrefix[0] = '\0';
  }
  else
  {
    strcpy(_topicPrefix, prefix);
  }
}

void OXRS_MQTT::setTopicSuffix(const char * suffix)
{ 
  if (suffix == NULL) 
  {
    _topicSuffix[0] = '\0';
  }
  else
  {
    strcpy(_topicSuffix, suffix);
  }
}

char * OXRS_MQTT::getWildcardTopic(char topic[])
{
  return _getTopic(topic, "+");
}

char * OXRS_MQTT::getLwtTopic(char topic[])
{
  sprintf_P(topic, PSTR("%s/%s"), getStatusTopic(topic), "lwt");
  return topic;
}

char * OXRS_MQTT::getAdoptTopic(char topic[])
{
  sprintf_P(topic, PSTR("%s/%s"), getStatusTopic(topic), "adopt");
  return topic;
}

char * OXRS_MQTT::getConfigTopic(char topic[])
{
  return _getTopic(topic, MQTT_CONFIG_TOPIC);
}

char * OXRS_MQTT::getCommandTopic(char topic[])
{
  return _getTopic(topic, MQTT_COMMAND_TOPIC);
}

char * OXRS_MQTT::getStatusTopic(char topic[])
{
  return _getTopic(topic, MQTT_STATUS_TOPIC);
}

char * OXRS_MQTT::getTelemetryTopic(char topic[])
{
  return _getTopic(topic, MQTT_TELEMETRY_TOPIC);
}

void OXRS_MQTT::onConnected(voidCallback callback)
{ 
  _onConnected = callback;
}

void OXRS_MQTT::onDisconnected(voidCallback callback)
{ 
  _onDisconnected = callback;
}

void OXRS_MQTT::onConfig(jsonCallback callback)
{ 
  _onConfig = callback;
}

void OXRS_MQTT::onCommand(jsonCallback callback)
{ 
  _onCommand = callback;
}

void OXRS_MQTT::loop(void)
{
  if (_client->loop())
  {
    // Currently connected so ensure we are ready to reconnect if it drops
    _backoff = 0;
    _lastReconnectMs = millis();
  }
  else
  {
    // Calculate the backoff interval and check if we need to try again
    uint32_t backoffMs = (uint32_t)_backoff * MQTT_BACKOFF_SECS * 1000;
    if ((millis() - _lastReconnectMs) > backoffMs)
    {
      if (strlen(_broker) == 0)
      {
        // No broker configured, so backoff
        if (_backoff < MQTT_MAX_BACKOFF_COUNT) { _backoff++; }
        _lastReconnectMs = millis();

        Serial.print(F("[mqtt] no broker configured, retry in "));
        Serial.print(_backoff * MQTT_BACKOFF_SECS);
        Serial.println(F("s"));
      }
      else
      {
        // Attempt to connect
        Serial.print(F("[mqtt] connecting to "));
        Serial.print(_broker);
        Serial.print(F(":"));
        Serial.print(_port);      
        if (strlen(_username) > 0)
        {
          Serial.print(F(" as "));
          Serial.print(_username);
        }
        Serial.println(F("..."));
      
        int state = _connect();
        if (state == 0) 
        {
          // Connection successful
          Serial.println(F("[mqtt] connected"));
        }
        else
        {
          // Reconnection failed, so backoff
          if (_backoff < MQTT_MAX_BACKOFF_COUNT) { _backoff++; }
          _lastReconnectMs = millis();

          Serial.print(F("[mqtt] failed to connect with error "));
          Serial.print(state);
          Serial.print(F(", retry in "));
          Serial.print(_backoff * MQTT_BACKOFF_SECS);
          Serial.println(F("s"));
        }
      }
    }
  }
}

void OXRS_MQTT::receive(char * topic, byte * payload, unsigned int length)
{
  // Log each received message to serial for debugging
  Serial.print(F("[mqtt] [rx] "));
  Serial.print(topic);
  Serial.print(F(" "));
  if (length == 0)
  {
    Serial.println(F("null"));
  }
  else
  {
    for (int i = 0; i < length; i++)
    {
      Serial.print((char)payload[i]);
    }
    Serial.println();
  }
  
  // Ignore if an empty message
  if (length == 0) return;

  // Tokenise the topic (skipping any prefix) to get the root topic type
  char * topicType;
  topicType = strtok(&topic[strlen(_topicPrefix)], "/");

  DynamicJsonDocument json(2048);
  DeserializationError error = deserializeJson(json, payload);
  if (error) 
  {
    Serial.print(F("[mqtt] failed to deserialise JSON: "));
    Serial.println(error.f_str());
    return;
  }

  // Process JSON payload
  if (json.is<JsonArray>())
  {
    JsonArray array = json.as<JsonArray>();
    for (JsonVariant v : array)
    {
      _fireCallback(topicType, v.as<JsonObject>());
    }
  }
  else
  {
    _fireCallback(topicType, json.as<JsonObject>());
  }
}

void OXRS_MQTT::reconnect(void)
{
  // Disconnect from MQTT broker
  _client->disconnect();
  
  // Force a connect attempt immediately
  _backoff = 0;
  _lastReconnectMs = millis();
}

boolean OXRS_MQTT::publishAdopt(JsonObject json)
{
  char topic[64];
  return _publish(json, getAdoptTopic(topic), true);
}

boolean OXRS_MQTT::publishStatus(JsonObject json)
{
  char topic[64];
  return _publish(json, getStatusTopic(topic), false);
}

boolean OXRS_MQTT::publishTelemetry(JsonObject json)
{
  char topic[64];
  return _publish(json, getTelemetryTopic(topic), false);
}

int OXRS_MQTT::_connect(void)
{
  // Set the broker address and port (in case they have changed)
  _client->setServer(_broker, _port);

  // Get our LWT topic
  char lwtTopic[64];
  getLwtTopic(lwtTopic);
  
  // Build our LWT payload
  StaticJsonDocument<16> lwtPayload;
  lwtPayload["online"] = false;
  
  // Get our LWT offline payload as raw string
  char lwtOfflinePayload[18];
  serializeJson(lwtPayload, lwtOfflinePayload);
  
  // Attempt to connect to the MQTT broker
  boolean success = _client->connect(_clientId, _username, _password, lwtTopic, 0, true, lwtOfflinePayload);
  if (success)
  {
    // Subscribe to our config and command topics
    char topic[64];

    Serial.print(F("[mqtt] subscribing to "));
    getConfigTopic(topic);
    Serial.println(topic);
    _client->subscribe(topic);
    
    Serial.print(F("[mqtt] subscribing to "));
    getCommandTopic(topic);
    Serial.println(topic);
    _client->subscribe(topic);

    // Publish our LWT online payload now we are ready
    lwtPayload["online"] = true;
    _publish(lwtPayload.as<JsonObject>(), lwtTopic, true);
 
    // Fire the connected callback
    if (_onConnected) { _onConnected(); }
  }
  else
  {
    // Fire the disconnected callback
    if (_onDisconnected) { _onDisconnected(); }
  }

  return _client->state();
}

void OXRS_MQTT::_fireCallback(const char * topicType, JsonObject json)
{
  // Forward to the appropriate callback
  if (strncmp(topicType, MQTT_CONFIG_TOPIC, 4) == 0)
  {
    if (_onConfig) 
    {
      _onConfig(json);
    }
    else
    {
      Serial.println(F("[mqtt] no config handler, ignoring message"));
    }
  }
  else if (strncmp(topicType, MQTT_COMMAND_TOPIC, 4) == 0)
  {
    if (_onCommand)
    {
      _onCommand(json);
    }
    else
    {
      Serial.println(F("[mqtt] no command handler, ignoring message"));
    }
  }
  else
  {
    Serial.println(F("[mqtt] invalid topic, ignoring message"));
  }  
}

char * OXRS_MQTT::_getTopic(char topic[], const char * topicType)
{
  if (strlen(_topicPrefix) == 0)
  {
    if (strlen(_topicSuffix) == 0)
    {
      sprintf_P(topic, PSTR("%s/%s"), topicType, _clientId);
    }
    else
    {
      sprintf_P(topic, PSTR("%s/%s/%s"), topicType, _clientId, _topicSuffix);
    }
  }
  else
  {
    if (strlen(_topicSuffix) == 0)
    {
      sprintf_P(topic, PSTR("%s/%s/%s"), _topicPrefix, topicType, _clientId);
    }
    else
    {
      sprintf_P(topic, PSTR("%s/%s/%s/%s"), _topicPrefix, topicType, _clientId, _topicSuffix);
    }
  }
  
  return topic;
}

boolean OXRS_MQTT::_publish(JsonObject json, char * topic, boolean retained)
{
  if (!_client->connected()) { return false; }
  
  char buffer[2048];
  serializeJson(json, buffer);
  
  // Log each published message to serial for debugging
  Serial.print(F("[mqtt] [tx] "));
  Serial.print(topic);
  Serial.print(F(" "));
  Serial.print(buffer);
  if (retained) { Serial.print(F(" [retained]")); }
  Serial.println();
  
  _client->publish(topic, buffer, retained);
  return true;
}