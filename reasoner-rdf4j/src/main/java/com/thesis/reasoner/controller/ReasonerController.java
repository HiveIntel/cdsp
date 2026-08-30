package com.thesis.reasoner.controller;

import com.thesis.reasoner.service.ReasonerService;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import java.util.HashSet;
import java.util.Set;

@RestController
public class ReasonerController {

    private static final Logger logger = LoggerFactory.getLogger(ReasonerController.class);
    private final ReasonerService reasonerService;
    private final Set<String> datastores = new HashSet<>();

    public ReasonerController(ReasonerService reasonerService) {
        this.reasonerService = reasonerService;
    }

    // Returns list of available datastores
    @GetMapping("/datastores")
    public ResponseEntity<String> listDatastores() {
        return ResponseEntity.ok(String.join(", ", datastores));
    }

    // Creates new datastore if needed
    @PostMapping("/datastores/{name}")
    public ResponseEntity<String> createDatastore(@PathVariable String name) {
        logger.info("Creating datastore: {}", name);
        datastores.add(name);
        return ResponseEntity.ok("Datastore " + name + " created.");
    }

    // Called by Reasoner Adapter to push incoming VSS data into the reasoner, allow multiple MIME types for robustness
    @PostMapping(value = "/datastores/{name}/content", consumes = { "text/turtle", "text/plain", "*/*" })
    public ResponseEntity<String> importContent(@PathVariable String name, @RequestBody String rdfData) {
        logger.debug("Received data chunk ({} chars) for datastore: {}", rdfData.length(), name);
        reasonerService.addData(rdfData);
        return ResponseEntity.ok("Content imported successfully.");
    }

    // SPARQL endpoint for KL
    @PostMapping(value = "/datastores/{name}/sparql", consumes = { "application/sparql-query",
            "application/sparql-update", "text/plain", "*/*" })
    public ResponseEntity<String> query(@PathVariable String name,
            @RequestBody String sparqlQuery, // Content of s1_output.rq
            @RequestHeader(value = "Accept", defaultValue = "*/*") String acceptHeader) {

        logger.info("Executing SPARQL query on datastore: {} [Accept: {}]", name, acceptHeader);
        // TripleAssembler requests 'text/tab-separated-values' for internal logic.
        // JSONWriter requests 'application/sparql-results+json' for output.
        if (acceptHeader.contains("json")) {
            return ResponseEntity.ok()
                    .header("Content-Type", "application/sparql-results+json")
                    .body(reasonerService.inferJson(sparqlQuery));
        } else {
            return ResponseEntity.ok()
                    .header("Content-Type", "text/tab-separated-values")
                    .body(reasonerService.inferTsv(sparqlQuery));
        }
    }

    // Endpoint to reset rules: replaces all previously added rules with one single new rule
    @PostMapping(value = "/datastores/{name}/rules/reset", consumes = { "text/turtle", "text/plain", "*/*" })
    public ResponseEntity<String> resetRules(@PathVariable String name, @RequestBody String newRuleData) {
        logger.info("Resetting rules for datastore: {}", name);
        try {
            reasonerService.resetRules(newRuleData);
            return ResponseEntity.ok("Rules reset successfully.");
        } catch (Exception e) {
            logger.error("Failed to reset rules", e);
            return ResponseEntity.badRequest().body("Failed to reset rules: " + e.getMessage());
        }
    }

    // Endpoint to append rules: note that each rule must have a corresponding .rq file (examples/knowledgelayer-hello-world/KL-config/queries/output/NEWRULE.rq)
    @PostMapping(value = "/datastores/{name}/rules/add", consumes = { "text/turtle", "text/plain", "*/*" })
    public ResponseEntity<String> addRules(@PathVariable String name, @RequestBody String newRuleData) {
        logger.info("Appending rules for datastore: {}", name);
        try {
            reasonerService.addRules(newRuleData);
            return ResponseEntity.ok("Rules appended successfully.");
        } catch (Exception e) {
            logger.error("Failed to append rules", e);
            return ResponseEntity.badRequest().body("Failed to append rules: " + e.getMessage());
        }
    }
}