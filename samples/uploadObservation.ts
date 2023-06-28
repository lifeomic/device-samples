/*
example:

yarn uploadObservation \
  --deviceName=<device_name> \
  --payloadValue=100 \
  --unit=% \
  --code=9750d5e5-d766-4793-a97d-16179578cc4d # battery observation \
  --codeSystem=http://lifeomic.com/fhir/devices \
  --codeDisplay="Battery Percentage"
*/
import { mqtt5, iot } from "aws-iot-device-sdk-v2";
import { ICrtError } from "aws-crt";
import { once } from "events";
import { parseArgs, ParseArgsConfig } from "util";
import { z } from "zod";
import { DEFAULT_ENDPOINT } from "./common";

function createClientConfig(args: {
  endpoint: string;
  cert: string;
  key: string;
  deviceName: string;
}): mqtt5.Mqtt5ClientConfig {
  const builder =
    iot.AwsIotMqtt5ClientConfigBuilder.newDirectMqttBuilderWithMtlsFromPath(
      args.endpoint,
      args.cert,
      args.key
    );

  builder.withConnectProperties({
    keepAliveIntervalSeconds: 1200,
    clientId: args.deviceName,
  });

  return builder.build();
}

type Args = {
  topic: string;
  payload: {
    value: number;
    unit: string;
    coding: {
      code: string;
      system: string;
      display: string;
    }[];
  };
  endpoint: string;
  deviceName: string;
};

function createClient(args: {
  endpoint: string;
  cert: string;
  key: string;
  deviceName: string;
}): mqtt5.Mqtt5Client {
  const config: mqtt5.Mqtt5ClientConfig = createClientConfig(args);

  console.log("Creating client for " + config.hostName);
  const client: mqtt5.Mqtt5Client = new mqtt5.Mqtt5Client(config);

  client.on("error", (error: ICrtError) => {
    console.log("Error event: " + error.toString());
  });

  client.on("attemptingConnect", (eventData: mqtt5.AttemptingConnectEvent) => {
    console.log("Attempting Connect event", eventData);
  });

  client.on("connectionSuccess", (eventData: mqtt5.ConnectionSuccessEvent) => {
    console.log("Connection Success event");
    console.log("Connack: " + JSON.stringify(eventData.connack));
    console.log("Settings: " + JSON.stringify(eventData.settings));
  });

  client.on("connectionFailure", (eventData: mqtt5.ConnectionFailureEvent) => {
    console.log("Connection failure event: " + eventData.error.toString());
    if (eventData.connack) {
      console.log("Connack: " + JSON.stringify(eventData.connack));
    }
  });

  client.on("disconnection", (eventData: mqtt5.DisconnectionEvent) => {
    console.log("Disconnection event: " + eventData.error.toString());
    if (eventData.disconnect !== undefined) {
      console.log("Disconnect packet: " + JSON.stringify(eventData.disconnect));
    }
  });

  client.on("stopped", (eventData: mqtt5.StoppedEvent) => {
    console.log("Stopped event", eventData);
  });

  return client;
}

async function run(args: Args) {
  const client: mqtt5.Mqtt5Client = createClient({
    cert: `./certs/${args.deviceName}.pem`,
    key: `./certs/${args.deviceName}.key`,
    deviceName: args.deviceName,
    endpoint: args.endpoint,
  });
  const connectionSuccess = once(client, "connectionSuccess");
  client.start();
  await connectionSuccess;

  const qos0PublishResult = await client.publish({
    qos: mqtt5.QoS.AtLeastOnce,
    topicName: args.topic,
    payload: JSON.stringify(args.payload),
    userProperties: [],
  });
  console.log(
    "AtLeastOnce Publish result: ",
    JSON.stringify(qos0PublishResult)
  );

  const stopped = once(client, "stopped");

  client.stop();

  await stopped;

  client.close();
}

const main = async () => {
  const options: ParseArgsConfig["options"] = {
    topic: { type: "string", default: "$aws/rules/FHIRIngest" },
    endpoint: { type: "string", default: DEFAULT_ENDPOINT },
    payloadValue: { type: "string" },
    deviceName: { type: "string" },
    unit: { type: "string" },
    code: { type: "string" },
    codeSystem: { type: "string" },
    codeDisplay: { type: "string" },
  };

  const argSchema = z
    .object({
      topic: z.string(),
      payloadValue: z.string().transform((arg) => parseInt(arg)),
      endpoint: z.string(),
      deviceName: z.string(),
      unit: z.string(),
      code: z.string(),
      codeSystem: z.string(),
      codeDisplay: z.string(),
    })
    .transform((args) => {
      return {
        ...args,
        payload: {
          value: args.payloadValue,
          unit: args.unit,
          coding: [
            {
              code: args.code,
              system: args.codeSystem,
              display: args.codeDisplay,
            },
          ],
        },
      };
    });

  const { values } = parseArgs({ options, args: process.argv.slice(2) });
  const args = argSchema.parse({
    ...values,
  });

  // make it wait as long as possible once the promise completes we'll turn it off.
  const timer = setTimeout(() => {}, 2147483647);

  await run(args);

  clearTimeout(timer);

  process.exit(0);
};

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
