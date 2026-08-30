# Knowledge Layer "Hello World" Use Case: Driving Style Detector

## Why?

This repository demonstrates a simple "Hello World" application of the Knowledge Layer, whose joint development was [proposed](https://wiki.covesa.global/pages/viewpage.action?pageId=71074417) in the COVESA Data Architecture working group. Given the generic nature of the underlying architecture, this use case can serve as a starting point for implementing more complex scenarios across various domains. One major advantage is that the use case is built on logical [playground components](../../cdsp/README.md). These components can often be swapped out (based on availability of alternatives) without significantly impacting other components (sometimes even not at all, e.g., whether using [IoTDB](../../cdsp/information-layer/handlers/src/iotdb/README.md) or other database component, it does not affect the Knowledge Layer).

### Key Aspects Illustrated:
- **Standardized Data Models:** Using models like [VSS](https://github.com/COVESA/vehicle_signal_specification/) and associated tools enables scalable, generic transformation of data into other formats, like graph data formats.
- **Logic Mapping in Data-Driven Rules:** Instead of hardcoded IF-ELSE logic, data-driven rules like [Datalog](https://en.wikipedia.org/wiki/Datalog) offer:
  - Easier maintenance of logic
  - Functional logic without the need for compilation
  - Smaller size for necessary updates
  - Easier traceability of decisions through declarative logic description
  - Centralized executable knowledge instead of scattered functional logic
- **No Over-Reliance on Machine Learning:** AI is not only Machine Learning, and Machine Learning isn't always the solution.
- **Uniform Implementation Pattern:** Offers a domain-independent pattern for other use cases.

## What?

In this use case, we have implemented an AI-powered solution to detect aggressive driving behaviours. Rather than relying on complex, maintenance-intensive IF-ELSE logic in code or overly sophisticated machine learning models, the detection is achieved through straightforward, data-driven rules.

## How?

This document describes the former JSON-RDF-Convertor/RDFox prototype. That
runtime was retired with the legacy connector. Use the maintained
[driving-style](../driving-style/README.md) example instead: it maps Information
Layer WebSocket data to RDF and evaluates it through the RDF4J HTTP reasoner.

### Implementation Details

#### Logical Components and Implementation Decisions:
- **Data Model:** [VSS](https://github.com/COVESA/vehicle_signal_specification/) - Describes vehicle data in a standardized format
- **Simulator:** [Remotive Labs](../../examples/remotivelabs-feeder/README.md) - Provides raw data for a test drive
- **DB Handler:** [IoTDB](../../cdsp/information-layer/handlers/src/iotdb/README.md) - Abstracts data store details
- **DB Router:** [WebSocket Server](../../cdsp/information-layer/router/src/websocket-server.ts) - Provides read,
write and subscribe capabilities to VSS data via a websocket server
- **Connector:** [Knowledge Layer](../../cdsp/knowledge-layer/README.md) - Uses
  `WebSocketClient -> RdfMapper -> ReasonerService`.
- **Reasoner Adapter:** RDF4J HTTP service.
- **Rules Language:** [Datalog](https://en.wikipedia.org/wiki/Datalog) - Allows describing IF-ELSE like rules in data-near language
- **Knowledge Graph and Reasoner:** RDF4J - Stores transformed data and applies
  the configured rules.

![The Use Case in a DIKW,logical and implementation view](KL-example-readme-graphic.png)

#### Input, Use Case Logic and Output:

Our input data for the use case includes:
- `Vehicle.Chassis.SteeringWheel.Angle`: The current angle of the steering wheel.
- `Vehicle.CurrentLocation.Latitude`: The latitude of the vehicle's current location.
- `Vehicle.CurrentLocation.Longitude`: The longitude of the vehicle's current location.
- `Vehicle.Speed`: The current speed of the vehicle.

The core logic of our use case is represented by the following rule (here in natural language for better readability):

<pre data-toolbar-order="disclaimer,copy-code,show-language" class="language-plaintext" style="border-radius: 0.5rem;"><code class="language-plaintext">If Vehicle.Chassis.SteeringWheel.Angle changes by more than 90 degrees in less than 3 seconds and Vehicle.Speed is greater than 50 km/h, then flag as aggressive driving.</code></pre>

Derived output data:
- `Vehicle.AI.Reasoner.InferenceResults`: Infered driving style details are written into this data node

## Installation and Running

### Archived setup

1. Create a [Remotive Labs](https://remotivelabs.com/) account.
2. Navigate to `Recordings`, select `Night drive to Luftkastellet`, and start the recording with the `Play` button. Wait until it starts playing, then stop the recording for now. This creates a broker for you, and we need its credentials next.
3. Navigate to the `Brokers` section and open `My personal broker`.
4. Copy the URL and API key to the environment variables in the [.env](.env) file: `REMOTIVE_LABS_BROKER_URL` and `REMOTIVE_LABS_BROKER_API_KEY`.
5. Configure the Information Layer with the respective database you want to use, e.g. IoTDB, by adding another `.env` file like described [here](../../cdsp/information-layer/handlers/src/README.md).
6. The former RDFox-specific steps are no longer applicable.

### Run Use Case

1. Log in to [Remotive Labs](https://remotivelabs.com/).
2. Select `Night drive to Luftkastellet` in the `Recordings` section, open the `Files` tab, and drag and drop the zip file `examples/shared-config/remotive-labs/config_vss_aggressive_driving.zip` into the `Signal Transformations` section.
3. Navigate to the `Playback` tab, select `config_vss_aggressive_driving` as the `signal transformation`, and press the `Play` button.
4. If the recording starts playing, stop it for now to proceed with setup.
5. Follow the runnable instructions in [driving-style](../driving-style/README.md).
---

Feel free to explore the components and rules used in this "Hello World" example to understand the basic implementation of the Knowledge Layer. You can extend this use case to more complex scenarios and even adapt it to different domains.
