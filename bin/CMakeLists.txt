# Copy files from here to the binary directory.
set( FILES_TO_INSTALL
  alignment_chunks
  alignments_get_best_n
  alignment_locs
  alignment_summary_stats
  alignments_maligner_to_malign_viz  
  alignments_maligner_to_soma
  alignments_soma_to_maligner
  alignments_split_by_reference
  concat_alns_files
  convert_maps
  estimate_cut_rate
  filter_alignments
  filter_maps_file
  generate_random_maps
  get_maps_by_id
  get_boundary_frags
  make_insilico_map
  parse_nucmer_coords
  parse_somacbcb_locs
  permute_maps_file
  sample_alignments
  sample_maps_file
  smooth_maps_file
  soma_consensus_maps
  soma_query_maps
  split_maps_file
  summarize_maps_file
  tidy_maps_file
)

install(FILES ${FILES_TO_INSTALL}
        DESTINATION ${MALIGNER_BIN_DIR}
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  
