# WebRTC / WHIP

Users can send video and audio from a web browser to OvenMediaEngine via WebRTC without requiring any plug-ins. In addition to browsers, any encoder that supports WebRTC transmission can also be used as a media source.

<table><thead><tr><th width="290">Title</th><th>Functions</th></tr></thead><tbody><tr><td>Container</td><td>RTP / RTCP</td></tr><tr><td>Security</td><td>DTLS, SRTP</td></tr><tr><td>Transport</td><td>ICE</td></tr><tr><td>Error Correction</td><td>ULPFEC (VP8, H.264), In-band FEC (Opus)</td></tr><tr><td>Codec</td><td>VP8, H.264, H.265, Opus</td></tr><tr><td>Signaling</td><td>Self-Defined Signaling Protocol, Embedded WebSocket-based Server / WHIP</td></tr><tr><td>Additional Features</td><td>Simulcast</td></tr></tbody></table>

## Configuration

### Bind

OvenMediaEngine supports self-defined signaling protocol and [WHIP ](https://datatracker.ietf.org/doc/draft-ietf-wish-whip/)for WebRTC ingest.&#x20;

```markup
<Bind>
    <Providers>
        ...
        <WebRTC>
            <Signalling>
                <Port>3333</Port>
                <TLSPort>3334</TLSPort>
            </Signalling>
            <IceCandidates>
                <TcpRelay>*:3478</TcpRelay>
                <TcpForce>false</TcpForce>
                <IceCandidate>*:10000-10005/udp</IceCandidate>
            </IceCandidates>
        </WebRTC>
    </Providers>
```

You can set the port to use for signaling in `<Bind><Provider><WebRTC><Signaling>`. `<Port>` is for setting an unsecured HTTP port, and `<TLSPort>` is for setting a secured HTTP port that is encrypted with TLS.&#x20;

For WebRTC ingest, you must set the ICE candidates of the OvenMediaEnigne server to `<IceCandidates>`. The candidates set in `<IceCandate>` are delivered to the WebRTC peer, and the peer requests communication with this candidate. Therefore, you must set the IP that the peer can access. If the IP is specified as `*`, OvenMediaEngine gathers all IPs of the server and delivers them to the peer.

`<TcpRelay>` means OvenMediaEngine's built-in TURN Server. When this is enabled, the address of this turn server is passed to the peer via self-defined signaling protocol or WHIP, and the peer communicates with this turn server over TCP. This allows OvenMediaEngine to support WebRTC/TCP itself. For more information on URL settings, check out [WebRTC over TCP](webrtc.md#webrtc-over-tcp).

### Application

WebRTC input can be turned on/off for each application. As follows Setting enables the WebRTC input function of the application. The `<CrossDomains>` setting is used in WebRTC signaling.

```markup
<Applications>
    <Application>
        <Name>app</Name>
        <Providers>
            <WebRTC>
                <Timeout>30000</Timeout>
                <CrossDomains>
                    <Url>*</Url>
                </CrossDomains>
            </WebRTC>
```

## URL Pattern

OvenMediaEnigne supports self-defined signaling protocol and WHIP for WebRTC ingest.

### Self-defined Signaling URL

The signaling URL for WebRTC ingest uses the query string `?direction=send` as follows to distinguish it from the url for WebRTC playback. Since the self-defined WebRTC signaling protocol is based on WebSocket, you must specify ws\[s] as the scheme.

> ws\[s]://\<host>\[:signaling port]/\<app name>/\<stream name>**?direction=send**

### WHIP URL

For ingest from the WHIP client, put `?direction=whip` in the query string in the signaling URL as in the example below. Since WHIP is based on HTTP, you must specify http\[s] as the scheme.

> http\[s]://\<host>\[:signaling port]/\<app name>/\<stream name>**?direction=whip**

### WebRTC over TCP

WebRTC transmission is sensitive to packet loss because it affects all players who access the stream. Therefore, it is recommended to provide WebRTC transmission over TCP. OvenMediaEngine has a built-in TURN server for WebRTC/TCP, and receives or transmits streams using the TCP session that the player's TURN client connects to the TURN server as it is. To use WebRTC/TCP, use transport=tcp query string as in WebRTC playback. See [WebRTC/tcp playback](../streaming/webrtc-publishing.md#webrtc-over-tcp) for more information.

> ws\[s]://\<host>\[:port]/\<app name>/\<stream name>**?direction=send\&transport=tcp**
>
> http\[s]://\<host>\[:port]/\<app name>/\<stream name>**?direction=whip\&transport=tcp**

{% hint style="warning" %}
To use WebRTC/tcp, `<TcpRelay>` must be turned on in `<Bind>` setting.&#x20;

If `<TcpForce>` is set to true, it works over TCP even if you omit the `?transport=tcp` query string from the URL.
{% endhint %}

## Simulcast

Simulcast is a feature that allows the sender to deliver multiple layers of quality to the end viewer without relying on a server encoder. This is a useful feature that allows for high-quality streaming to be delivered to viewers while significantly reducing costs in environments with limited server resources.

OvenMediaEngine supports WebRTC simulcast since 0.18.0. OvenMediaEngine only supports simulcast with WHIP signaling, and not with OvenMediaEngine's own signaling. Simulcast is only supported with WHIP signaling, and is not supported with OvenMediaEngine's own defined signaling.

You can test this using an encoder that supports WHIP and simulcast, such as OvenLiveKit or OBS. You can usually set the number of layers as below, and if you use the OvenLiveKit API directly, you can also configure the resolution and bitrate per layer.

<figure><img src="../.gitbook/assets/image (45).png" alt=""><figcaption></figcaption></figure>

<figure><img src="../.gitbook/assets/image (46).png" alt=""><figcaption></figcaption></figure>

<figure><img src="../.gitbook/assets/image (47).png" alt=""><figcaption></figcaption></figure>

### Playlist Template for Simulcast

When multiple input video Tracks exist, it means that several Tracks with the same Variant Name are present. For example, consider the following basic `OutputProfile` and assume there are three input video Tracks. In this case, three Tracks with the Variant Name `video_bypass` will be created:

```xml
<OutputProfile>
	<Name>stream</Name>
	<OutputStreamName>${OriginStreamName}</OutputStreamName>
	<Encodes>
		<Video>
			<Name>video_bypass</Name>
			<Bypass>true</Bypass>
		</Video>
	</Encodes>
</OutputProfile>
```

How can we structure Playlists with multiple Tracks? A simple method introduces an `Index` concept in Playlists:

```xml
<Playlist>
	<Name>simulcast</Name>
	<FileName>template</FileName>
	<Options>
		<WebRtcAutoAbr>true</WebRtcAutoAbr>
		<HLSChunklistPathDepth>0</HLSChunklistPathDepth>
		<EnableTsPackaging>true</EnableTsPackaging>
	</Options>
	<Rendition>
		<Name>first</Name>
		<Video>video_bypass</Video>
		<VideoIndexHint>0</VideoIndexHint> <!-- Optional, default : 0 -->
		<Audio>aac_audio</Audio>
	</Rendition>
	<Rendition>
		<Name>second</Name>
		<Video>video_bypass</Video>
		<VideoIndexHint>1</VideoIndexHint> <!-- Optional, default : 0 -->
		<Audio>aac_audio</Audio>
		<AudioIndexHint>0</AudioIndexHint> <!-- Optional, default : 0 -->
	</Rendition>
</Playlist>
```

`VideoIndexHint` and `AudioIndexHint` specify the Index of input video and audio Tracks, respectively.

However, when using the above configuration, if the encoder broadcasts 3 video tracks with Simulcast, it is inconvenient to change the configuration and restart the server to provide HLS/WebRTC streaming with 3 ABR layers. So I implemented a dynamic Rendition tool called RenditionTemplate.



### RenditionTemplate

The `RenditionTemplate` feature automatically generates Renditions based on specified conditions, eliminating the need to define each one manually. Here’s an example:

```xml
<Playlist>
	<Name>template</Name>
	<FileName>template</FileName>
	<Options>
		<WebRtcAutoAbr>true</WebRtcAutoAbr>
		<HLSChunklistPathDepth>0</HLSChunklistPathDepth>
		<EnableTsPackaging>true</EnableTsPackaging>
	</Options>
	<RenditionTemplate>
		<Name>hls_${Height}p</Name>
		<VideoTemplate>
			<EncodingType>bypassed</EncodingType>
		</VideoTemplate>
		<AudioTemplate>
			<VariantName>aac_audio</VariantName>
		</AudioTemplate>
	</RenditionTemplate>
</Playlist>
```

This configuration creates Renditions for all bypassed videos and uses audio Tracks with the `aac_audio` Variant Name.\
The following macros can be used in the Name of a RenditionTemplate:\
`${Width}` | `${Height}` | `${Bitrate}` | `${Framerate}` | `${Samplerate}` | `${Channel}`

You can specify conditions to control Rendition creation. For example, to include only videos with a minimum resolution of 280p and bitrate above 500kbps, or to exclude videos exceeding 1080p or 2Mbps:

```xml
<VideoTemplate>
	<EncodingType>bypassed</EncodingType> <!-- all, bypassed, encoded -->
	<VariantName>bypass_video</VariantName>
	<VideoIndexHint>0</VideoIndexHint>
	<MaxWidth>1080</MaxWidth>
	<MinWidth>240</MinWidth>
	<MaxHeight>720</MaxHeight>
	<MinHeight>240</MinHeight>
	<MaxFPS>30</MaxFPS>
	<MinFPS>30</MinFPS>
	<MaxBitrate>2000000</MaxBitrate>
	<MinBitrate>500000</MinBitrate>
</VideoTemplate>
<AudioTemplate>
         <EncodingType>encoded</EncodingType> <!-- all, bypassed, encoded -->
	<VariantName>aac_audio</VariantName>
	<MaxBitrate>128000</MaxBitrate>
	<MinBitrate>128000</MinBitrate>
	<MaxSamplerate>48000</MaxSamplerate>
	<MinSamplerate>48000</MinSamplerate>
	<MaxChannel>2</MaxChannel>
	<MinChannel>2</MinChannel>
	<AudioIndexHint>0</AudioIndexHint>
</AudioTemplate>
```

## WebRTC Producer

We provide a demo page so you can easily test your WebRTC input. You can access the demo page at the URL below.

{% embed url="https://demo.ovenplayer.com/demo_input.html" %}

![](<../.gitbook/assets/image (4) (1).png>)

{% hint style="warning" %}
The getUserMedia API to access the local device only works in a [secure context](https://developer.mozilla.org/en-US/docs/Web/API/MediaDevices/getUserMedia#privacy_and_security). So, the WebRTC Input demo page can only work on the https site \*\*\*\* [**https**://demo.ovenplayer.com/demo\_input.html](https://demo.ovenplayer.com/demo_input.html). This means that due to [mixed content](https://developer.mozilla.org/en-US/docs/Web/Security/Mixed_content) you have to install the certificate in OvenMediaEngine and use the signaling URL as wss to test this. If you can't install the certificate in OvenMediaEngine, you can temporarily test it by allowing the insecure content of the demo.ovenplayer.com URL in your browser.
{% endhint %}

### Self-defined WebRTC Ingest Signaling Protocol

To create a custom WebRTC Producer, you need to implement OvenMediaEngine's Self-defined Signaling Protocol or WHIP. Self-defined protocol is structured in a simple format and uses the[ same method as WebRTC Streaming](../streaming/webrtc-publishing.md#signalling-protocol).

![](<../.gitbook/assets/image (10).png>)

When the player connects to ws\[s]://host:port/app/stream?**direction=send** through a WebSocket and sends a request offer command, the server responds to the offer SDP. If `transport=tcp` exists in the query string of the URL, [iceServers ](https://developer.mozilla.org/en-US/docs/Web/API/RTCIceServer)information is included in offer SDP, which contains the information of OvenMediaEngine's built-in TURN server, so you need to set this in `RTCPeerConnection` to use WebRTC/TCP. The player then `setsRemoteDescription` and `addIceCandidate` offer SDP, generates an answer SDP, and responds to the server.
