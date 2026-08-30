package com.thesis.reasoner.service;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.io.StringReader;
import java.io.ByteArrayOutputStream;
import java.util.List;
import java.util.ArrayList;
import java.util.Collections;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.ArrayBlockingQueue;
import org.eclipse.rdf4j.repository.Repository;
import org.eclipse.rdf4j.repository.RepositoryConnection;
import org.eclipse.rdf4j.query.resultio.sparqljson.SPARQLResultsJSONWriter;
import org.eclipse.rdf4j.query.resultio.text.tsv.SPARQLResultsTSVWriter;
import org.eclipse.rdf4j.query.TupleQuery;
import org.eclipse.rdf4j.query.GraphQuery;
import org.eclipse.rdf4j.query.GraphQueryResult;
import org.eclipse.rdf4j.query.Update;
import org.eclipse.rdf4j.rio.RDFFormat;
import org.eclipse.rdf4j.model.IRI;
import org.eclipse.rdf4j.model.Statement;
import org.eclipse.rdf4j.model.Value;
import org.eclipse.rdf4j.model.Literal;
import org.eclipse.rdf4j.repository.RepositoryResult;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;
import org.springframework.util.StopWatch;
import jakarta.annotation.PreDestroy;
import org.springframework.core.io.Resource;
import org.springframework.core.io.ResourceLoader;

@Service
public class ReasonerService {

    private static final Logger logger = LoggerFactory.getLogger(ReasonerService.class);
    private final Repository repository;
    private static final String SHACL_SHAPE_GRAPH_URI = "http://rdf4j.org/schema/rdf4j#SHACLShapeGraph";
    private final RepositoryConnection writeConnection;
    private final List<GraphQuery> precompiledRules = new ArrayList<>();
    private final List<String> cachedRuleStrings = new ArrayList<>();
    private final Update pruneUpdate;
    private int messageCount = 0;

    // OPTIMIZATION PARAMETERS
    private static final int PRUNING_INTERVAL = 70; // Prunes the RDF graph after addData() method has been called PRUNING_INTERVAL times
    private static final int MAX_OBSERVATIONS = 400; // Only keep the latest MAX_OBSERVATIONS observations in the memory
    
    private final ExecutorService reasoningExecutor = new ThreadPoolExecutor(
            1, 1, // 1. Thread Pool Size: Only 1 thread to process reasoning tasks
            0L, TimeUnit.MILLISECONDS, // 2. Keep-Alive Time: Not applicable since corePoolSize == maximumPoolSize
            new ArrayBlockingQueue<>(5000), // 3. The Queue: 5000 tasks max
            new ThreadPoolExecutor.CallerRunsPolicy() // 4. The Rejection Policy: If queue is full, run in main thread
    );

    public ReasonerService(Repository repository, ResourceLoader resourceLoader) {
        this.repository = repository;
        this.writeConnection = repository.getConnection();
        loadShapes(resourceLoader);

        for (String rule : cachedRuleStrings) {
            this.precompiledRules.add(this.writeConnection.prepareGraphQuery(rule));
        }
        // DELETE query for keeping the memory low (old observations are removed)
        String pruneQueryString = "PREFIX sosa: <http://www.w3.org/ns/sosa/>\n" +
                "DELETE {\n" +
                "  ?obs ?p ?o .\n" +
                "} WHERE {\n" +
                "  {\n" +
                "    SELECT ?obs WHERE {\n" +
                "      ?obs a sosa:Observation ;\n" +
                "           sosa:phenomenonTime ?pt .\n" +
                "    } ORDER BY DESC(?pt) OFFSET " + MAX_OBSERVATIONS + "\n" +
                "  }\n" +
                "  ?obs ?p ?o .\n" +
                "}";
        this.pruneUpdate = this.writeConnection.prepareUpdate(pruneQueryString);
    }

    // Helper to load rules from the shape graph into memory
    private void cacheRules(RepositoryConnection conn) {
        IRI shapeGraphIRI = conn.getValueFactory().createIRI(SHACL_SHAPE_GRAPH_URI);
        String SHACL_NS = "http://www.w3.org/ns/shacl#";
        IRI shRule = conn.getValueFactory().createIRI(SHACL_NS, "rule");
        IRI shConstruct = conn.getValueFactory().createIRI(SHACL_NS, "construct");
        IRI shOrder = conn.getValueFactory().createIRI(SHACL_NS, "order");

        List<Rule> rules = new ArrayList<>();
        try (RepositoryResult<Statement> result = conn
                .getStatements(null, shRule, null, shapeGraphIRI)) {
            for (Statement st : result) {
                org.eclipse.rdf4j.model.Resource ruleNode = (org.eclipse.rdf4j.model.Resource) st.getObject();
                // Get Order
                double order = 0.0;
                try (RepositoryResult<Statement> orderSts = conn
                        .getStatements(ruleNode, shOrder, null, shapeGraphIRI)) {
                    if (orderSts.hasNext()) {
                        Value o = orderSts.next().getObject();
                        if (o instanceof Literal) {
                            order = ((Literal) o).doubleValue();
                        }
                    }
                }
                // Get Construct Query
                try (RepositoryResult<Statement> constructSts = conn
                        .getStatements(ruleNode, shConstruct, null, shapeGraphIRI)) {
                    for (Statement constructSt : constructSts) {
                        rules.add(new Rule(constructSt.getObject().stringValue(), order));
                    }
                }
            }
        }
        Collections.sort(rules);
        cachedRuleStrings.clear();
        for (Rule r : rules) {
            cachedRuleStrings.add(r.queryString);
        }
        logger.info("Cached {} SHACL rules (sorted by sh:order).", cachedRuleStrings.size());
    }

    private void loadShapes(ResourceLoader resourceLoader) {
        // CONFIG_PATH designates the external directory mapped via Docker where the SHACL/DLOG rules and ontologies are stored
        String configPath = System.getenv("CONFIG_PATH");
        String prefix = (configPath != null && !configPath.isEmpty()) ? "file:" + configPath : "classpath:";

        try (RepositoryConnection conn = repository.getConnection()) {
            conn.begin();
            // Load Ontologies (sliding window config)
            String ontologyPath = prefix + (prefix.startsWith("file:") ? "/ontologies/sliding_window_config.ttl"
                    : "ontologies/sliding_window_config.ttl");
            logger.info("Loading Ontologies from: {}", ontologyPath);
            Resource ontologyResource = resourceLoader.getResource(ontologyPath);
            conn.add(ontologyResource.getInputStream(), "", RDFFormat.TURTLE);

            // Set initial rule, rule updates can be injected via the REST API, 
            String rulePath = prefix + (prefix.startsWith("file:") ? "/rules/driving_style_inference_rules_shacl.dlog"
                   : "rules/driving_style_inference_rules_shacl.dlog");
            logger.info("Loading SHACL Rules from: {}", rulePath);
            Resource ruleResource = resourceLoader.getResource(rulePath);
            conn.add(ruleResource.getInputStream(), "", RDFFormat.TURTLE,
                   conn.getValueFactory().createIRI(SHACL_SHAPE_GRAPH_URI));

            cacheRules(conn); // Extract and cache rules
            conn.commit();
            logger.info("SHACL shapes and rules loaded successfully.");
        } catch (IOException e) {
            throw new RuntimeException("Failed to load SHACL shapes/rules", e);
        }
    }

    // Reset SHACL rules: replaces all previously added rules with one single new rule
    public void resetRules(String newRuleData) {
        synchronized (writeConnection) {
            try {
                // Clear the old SHACL rules from the RDF repository memory
                IRI shapeGraphIRI = writeConnection.getValueFactory().createIRI(SHACL_SHAPE_GRAPH_URI);
                writeConnection.begin();
                writeConnection.clear(shapeGraphIRI);
                precompiledRules.clear();
                cachedRuleStrings.clear();
                // Inject new rule string back into the connection
                writeConnection.add(new StringReader(newRuleData), "", RDFFormat.TURTLE, shapeGraphIRI);
                cacheRules(writeConnection);
                for (String rule : cachedRuleStrings) {
                    precompiledRules.add(writeConnection.prepareGraphQuery(rule));
                }
                writeConnection.commit();
                logger.info("Successfully reset and compiled new SHACL rules.");
            } catch (Exception e) {
                if (writeConnection.isActive()) {
                    writeConnection.rollback();
                }
                logger.error("Error during rule reset", e);
                throw new RuntimeException("Failed to reset rules", e);
            }
        }
    }

    // Append SHACL rules to existing ones
    public void addRules(String newRuleData) {
        synchronized (writeConnection) {
            try {
                IRI shapeGraphIRI = writeConnection.getValueFactory().createIRI(SHACL_SHAPE_GRAPH_URI);
                writeConnection.begin();
                // Inject new rule string back into the connection (without clearing)
                writeConnection.add(new StringReader(newRuleData), "", RDFFormat.TURTLE, shapeGraphIRI);
                // Clear precompiled rules array so we can rebuild it
                precompiledRules.clear();
                cachedRuleStrings.clear();
                cacheRules(writeConnection);
                for (String rule : cachedRuleStrings) {
                    precompiledRules.add(writeConnection.prepareGraphQuery(rule));
                }
                writeConnection.commit();
                logger.info("Successfully appended and compiled new SHACL rules.");
            } catch (Exception e) {
                if (writeConnection.isActive()) {
                    writeConnection.rollback();
                }
                logger.error("Error during rule appending", e);
                throw new RuntimeException("Failed to append rules", e);
            }
        }
    }

    // Push data into the datastore asynchronously
    public void addData(String rdfData) {
        reasoningExecutor.submit(() -> {
            synchronized (writeConnection) {
                try {
                    // micro measurements
                    StopWatch watch = new StopWatch("addData Performance");

                    watch.start("Begin");
                    writeConnection.begin();
                    watch.stop();

                    watch.start("AddTriples");
                    writeConnection.add(new StringReader(rdfData), "", RDFFormat.TURTLE);
                    watch.stop();

                    watch.start("ExecuteRules");
                    executeShaclRules(writeConnection);
                    watch.stop();

                    watch.start("PruneData");
                    if (++messageCount >= PRUNING_INTERVAL) {
                        pruneOldData(writeConnection);
                        messageCount = 0;
                    }
                    watch.stop();

                    watch.start("Commit");
                    writeConnection.commit();
                    watch.stop();

                    // --- CSV LOG FOR PLOTTING ---
                    // StringBuilder csvLog = new StringBuilder("PERF_METRIC");
                    // csvLog.append(",").append(System.currentTimeMillis()); // Add a Unix
                    // timestamp for the X-axis

                    // for (StopWatch.TaskInfo task : watch.getTaskInfo()) {
                    // csvLog.append(",").append(task.getTimeNanos() / 1_000_000.0); // Get the
                    // times in milliseconds
                    // }
                    // logger.info(csvLog.toString());
                    // logger.debug("Data ingested and reasoning completed.");
                } catch (Exception e) {
                    if (writeConnection.isActive()) {
                        writeConnection.rollback();
                    }
                    logger.error("Error during data ingestion/reasoning", e);
                }
            }
        });
    }

    // Execute cached SHACL rules directly into the connection
    private void executeShaclRules(RepositoryConnection conn) {
        int ruleIndex = 0;
        IRI inferredContext = conn.getValueFactory().createIRI("http://example.org/inferred");

        conn.clear(inferredContext); // Clear previous inferences for the current sliding window

        for (GraphQuery ruleQuery : precompiledRules) {
            ruleIndex++;
            // Use try-with-resources to ensure the GraphQueryResult is safely closed
            try (GraphQueryResult result = ruleQuery.evaluate()) {
                conn.add((Iterable<Statement>) result, inferredContext);
                if (logger.isTraceEnabled()) {
                    logger.trace("Successfully executed Rule #{}", ruleIndex);
                }
            } catch (Exception e) {
                logger.error("Failed to execute SHACL rule #" + ruleIndex, e);
            }
        }
        if (logger.isDebugEnabled()) {
            logger.debug("Finished executing SHACL rules.");
        }
    }

    // Limits indefinite memory growth
    private void pruneOldData(RepositoryConnection conn) {
        try {
            pruneUpdate.execute();
        } catch (Exception e) {
            logger.error("Failed to prune old data", e);
        }
    }

    // JSONWriter needs JSON
    public String inferJson(String sparqlQuery) {
        //logger.info(sparqlQuery);
        try (RepositoryConnection conn = repository.getConnection()) {
            TupleQuery query = conn.prepareTupleQuery(sparqlQuery);
            try (ByteArrayOutputStream out = new ByteArrayOutputStream()) {
                SPARQLResultsJSONWriter writer = new SPARQLResultsJSONWriter(out);
                query.evaluate(writer);
                return out.toString(StandardCharsets.UTF_8);
            }
        } catch (Exception e) {
            logger.error("Error executing JSON inference", e);
            return "{\"head\":{\"vars\":[]},\"results\":{\"bindings\":[]}}";
        }
    }

    // TSV for TripleAssembler
    public String inferTsv(String sparqlQuery) {
        try (RepositoryConnection conn = repository.getConnection()) {
            TupleQuery query = conn.prepareTupleQuery(sparqlQuery);
            try (ByteArrayOutputStream out = new ByteArrayOutputStream()) {
                SPARQLResultsTSVWriter writer = new SPARQLResultsTSVWriter(out);
                query.evaluate(writer);
                return out.toString(StandardCharsets.UTF_8);
            }
        } catch (Exception e) {
            logger.error("Error executing TSV inference", e);
            return "";
        }
    }

    // Private helper class for sorting evaluation rules based on sh:order
    private static class Rule implements Comparable<Rule> {
        final String queryString;
        final double order;

        Rule(String queryString, double order) {
            this.queryString = queryString;
            this.order = order;
        }

        @Override
        public int compareTo(Rule rule) {
            return Double.compare(this.order, rule.order);
        }
    }

    @PreDestroy
    public void cleanUp() {
        logger.info("Shutting down reasoning executor and closing connections...");
        reasoningExecutor.shutdown();
        try {
            if (!reasoningExecutor.awaitTermination(5, TimeUnit.SECONDS)) {
                reasoningExecutor.shutdownNow();
            }
        } catch (InterruptedException e) {
            reasoningExecutor.shutdownNow();
        }
        if (writeConnection != null && writeConnection.isOpen()) {
            writeConnection.close();
        }
    }

}